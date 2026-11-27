// Platforms/Android/CameraXMeter.cs
// A színkalibrálás kamerája CameraX-szel: élő előnézet + képkocka-elemzés (ImageAnalysis, RGBA), az expozíció (AE) rögzítésével
// és napfény-fehéregyensúllyal, hogy a kamera ne "korrigálja" a LED színét a mérés közben.
using Android.Content;
using Android.Hardware.Camera2;
using AndroidX.Camera.Camera2.InterOp;
using AndroidX.Camera.Core;
using AndroidX.Camera.Core.ResolutionSelector;
using AndroidX.Camera.Lifecycle;
using AndroidX.Camera.View;
using AndroidX.Core.Content;
using AndroidX.Lifecycle;
using Controll.Pages;
using Java.Util.Concurrent;
using Microsoft.Maui.Handlers;

namespace Controll.Services;

public sealed class CameraPreviewHandler : ViewHandler<CameraPreviewView, PreviewView>
{
    public CameraPreviewHandler() : base(ViewHandler.ViewMapper) { }

    protected override PreviewView CreatePlatformView()
    {
        var pv = new PreviewView(Context);
        pv.SetImplementationMode(PreviewView.ImplementationMode.Compatible!);
        pv.SetScaleType(PreviewView.ScaleType.FitCenter!);
        return pv;
    }
}

public sealed class CameraXMeter : Java.Lang.Object, ICameraMeter, ImageAnalysis.IAnalyzer
{
    private ProcessCameraProvider? _provider;
    private ICamera? _camera;
    private IExecutorService? _executor;

    // sRGB (gamma) -> lineáris fény, bájtonként
    private static readonly float[] LinLut = BuildLut();
    private static float[] BuildLut()
    {
        var t = new float[256];
        for (int i = 0; i < 256; i++)
        {
            double c = i / 255.0;
            t[i] = (float)(c <= 0.04045 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4));
        }
        return t;
    }

    // A képkocka-gyűjtés állapota (az elemző szálról és a hívó szálról is érik)
    private readonly object _gate = new();
    private float[]? _acc;
    private int _need, _got;
    private TaskCompletionSource<float[]>? _tcs;
    private volatile float _lastClip;

    public static async Task<CameraXMeter> StartAsync(Context context, PreviewView previewView, ILifecycleOwner owner)
    {
        var meter = new CameraXMeter();
        await meter.BindAsync(context, previewView, owner);
        return meter;
    }

    private async Task BindAsync(Context context, PreviewView previewView, ILifecycleOwner owner)
    {
        var future = ProcessCameraProvider.GetInstance(context);
        _provider = await Task.Run(() => Java.Interop.JavaObjectExtensions.JavaCast<ProcessCameraProvider>(future.Get()!));

        var preview = new Preview.Builder().Build()!;
        preview.SetSurfaceProvider(ContextCompat.GetMainExecutor(context)!, previewView.SurfaceProvider);

        // 640x480 körüli felbontás elég (cellaátlagokat számolunk), gyors és kevés zajt visz
        var selector = new ResolutionSelector.Builder()
            .SetResolutionStrategy(new ResolutionStrategy(new Android.Util.Size(640, 480), ResolutionStrategy.FallbackRuleClosestHigherThenLower))!
            .Build()!;
        var analysis = new ImageAnalysis.Builder()
            .SetResolutionSelector(selector)!
            .SetBackpressureStrategy(ImageAnalysis.StrategyKeepOnlyLatest)!
            .SetOutputImageFormat(ImageAnalysis.OutputImageFormatRgba8888)!
            .Build()!;
        _executor = Executors.NewSingleThreadExecutor();
        analysis.SetAnalyzer(_executor, this);

        _provider.UnbindAll();
        _camera = _provider.BindToLifecycle(owner, CameraSelector.DefaultBackCamera!, preview, analysis);
    }

    public void Stop()
    {
        try { _provider?.UnbindAll(); } catch { }
        try { _executor?.Shutdown(); } catch { }
        _camera = null;
    }

    // ── Elemzés: minden képkockából 64x48 cellás lineáris R/G/B átlag ──
    public void Analyze(IImageProxy? image)
    {
        if (image == null) return;
        try
        {
            var plane = image.GetPlanes()![0];
            var buf = plane.Buffer!;
            int w = image.Width, h = image.Height, rowStride = plane.RowStride, pxStride = plane.PixelStride;
            var data = new byte[buf.Remaining()];
            buf.Get(data);

            var grid = new float[CameraGrid.Cells * 3];
            long clipped = 0, samples = 0;
            for (int cy = 0; cy < CameraGrid.H; cy++)
            {
                int y0 = cy * h / CameraGrid.H, y1 = Math.Max(y0 + 1, (cy + 1) * h / CameraGrid.H);
                for (int cx = 0; cx < CameraGrid.W; cx++)
                {
                    int x0 = cx * w / CameraGrid.W, x1 = Math.Max(x0 + 1, (cx + 1) * w / CameraGrid.W);
                    float r = 0, g = 0, b = 0; int n = 0;
                    for (int y = y0; y < y1; y += 2)
                    {
                        int row = y * rowStride;
                        for (int x = x0; x < x1; x += 2)
                        {
                            int p = row + x * pxStride;
                            if (p + 2 >= data.Length) continue;
                            byte br = data[p], bg = data[p + 1], bb = data[p + 2];
                            r += LinLut[br]; g += LinLut[bg]; b += LinLut[bb]; n++;
                            if (br >= 250 || bg >= 250 || bb >= 250) clipped++;
                            samples++;
                        }
                    }
                    int idx = (cy * CameraGrid.W + cx) * 3;
                    if (n > 0) { grid[idx] = r / n; grid[idx + 1] = g / n; grid[idx + 2] = b / n; }
                }
            }
            _lastClip = samples == 0 ? 0 : (float)clipped / samples;
            Deliver(grid);
        }
        catch (Exception ex) { System.Diagnostics.Debug.WriteLine($"CameraX elemzés: {ex.Message}"); }
        finally { image.Close(); }
    }

    private void Deliver(float[] grid)
    {
        TaskCompletionSource<float[]>? done = null;
        float[]? result = null;
        lock (_gate)
        {
            if (_tcs == null || _acc == null) return;
            for (int i = 0; i < _acc.Length; i++) _acc[i] += grid[i];
            _got++;
            if (_got >= _need)
            {
                for (int i = 0; i < _acc.Length; i++) _acc[i] /= _got;
                result = _acc; done = _tcs;
                _tcs = null; _acc = null;
            }
        }
        done?.TrySetResult(result!);
    }

    public async Task<float[]> GrabAsync(int frames, CancellationToken ct)
    {
        TaskCompletionSource<float[]> tcs;
        lock (_gate)
        {
            tcs = new TaskCompletionSource<float[]>(TaskCreationOptions.RunContinuationsAsynchronously);
            _acc = new float[CameraGrid.Cells * 3];
            _need = Math.Max(1, frames); _got = 0; _tcs = tcs;
        }
        using var reg = ct.Register(() => tcs.TrySetCanceled());
        var timeout = Task.Delay(5000, ct);
        var first = await Task.WhenAny(tcs.Task, timeout);
        if (first != tcs.Task)
        {
            lock (_gate) { _tcs = null; _acc = null; }
            ct.ThrowIfCancellationRequested();
            throw new System.TimeoutException("A kamera nem ad képet.");
        }
        return await tcs.Task;
    }

    // ── Expozíció: fehér fénynél kiégés nélkül, majd AE-zár + napfény-fehéregyensúly ──
    public async Task<bool> AutoExposeAndLockAsync(CancellationToken ct)
    {
        var cam = _camera ?? throw new InvalidOperationException("A kamera nincs elindítva.");
        SetOptions(cam, aeLock: false);
        await Task.Delay(1300, ct);

        var exposure = cam.CameraInfo!.ExposureState!;
        var range = exposure.ExposureCompensationRange!;
        int lower = (int)range.Lower!;
        int index = exposure.ExposureCompensationIndex;

        bool ok = false;
        for (int guard = 0; guard < 24; guard++)
        {
            ct.ThrowIfCancellationRequested();
            await GrabAsync(3, ct);
            if (_lastClip < 0.003f) { ok = true; break; }
            if (index <= lower) break;
            index--;
            cam.CameraControl!.SetExposureCompensationIndex(index);
            await Task.Delay(750, ct);
        }
        if (!ok) return false;

        // Egy lépés tartalék a kiégés ellen, majd rögzítés
        if (index > lower)
        {
            index--;
            cam.CameraControl!.SetExposureCompensationIndex(index);
            await Task.Delay(750, ct);
        }
        SetOptions(cam, aeLock: true);
        await Task.Delay(500, ct);
        return true;
    }

    private static void SetOptions(ICamera cam, bool aeLock)
    {
        var b = new CaptureRequestOptions.Builder();
        b.SetCaptureRequestOption(CaptureRequest.ControlAeLock!, Java.Lang.Boolean.ValueOf(aeLock));
        b.SetCaptureRequestOption(CaptureRequest.ControlAwbMode!, Java.Lang.Integer.ValueOf((int)ControlAwbMode.Daylight));
        Camera2CameraControl.From(cam.CameraControl!).SetCaptureRequestOptions(b.Build()!);
    }
}
