// Services/RgbCalibrationRunner.cs
// A teljes automatikus kalibrálás menete: az eszköz nyers vezérlése (CAL_RAW) + a kamera mérése + számítás + mentés az eszközre.
using System.Globalization;

namespace Controll.Services;

// A kamera, ahogy a futtató látja. Az Android megvalósítás (CameraX) a Platforms/Android mappában van.
public interface ICameraMeter
{
    // Beállítja az expozíciót úgy, hogy a (most világító) fehér fény ne égjen ki, majd rögzíti az expozíciót és a fehéregyensúlyt.
    // false: nem sikerült kiégés nélküli expozíciót találni (túl erős fény).
    Task<bool> AutoExposeAndLockAsync(CancellationToken ct);
    // A következő 'frames' db új képkocka átlaga: CameraGrid.W x CameraGrid.H cella, cellánként lineáris R, G, B (0..1).
    Task<float[]> GrabAsync(int frames, CancellationToken ct);
}

public static class CameraGrid
{
    public const int W = 64;
    public const int H = 48;
    public const int Cells = W * H;
}

public sealed record CalibrationProgress(double Fraction, string Text);

public sealed class CalibrationOutcome
{
    public bool Success { get; init; }
    public string Message { get; init; } = "";
    public RgbCalibrationResult? Result { get; init; }
    public double WhiteDeviation { get; init; }   // a mért fehér R/G/B közötti legnagyobb eltérés (0.03 = 3 %)
}

public sealed class RgbCalibrationRunner
{
    private readonly ICameraMeter _cam;
    private readonly Func<string, Task> _send;

    private const int SettleMs = 420;     // a LED átváltása + a kamera késése után ennyit várunk a mérés előtt
    private const int FramesPerStep = 3;
    private const double Tolerance = 0.03;
    private const int MaxRefine = 3;

    public RgbCalibrationRunner(ICameraMeter cam, Func<string, Task> send)
    {
        _cam = cam;
        _send = send;
    }

    private Task Raw(int r, int g, int b) => _send($"CAL_RAW:{r}:{g}:{b}");

    public async Task<CalibrationOutcome> RunAsync(IProgress<CalibrationProgress> progress, CancellationToken ct)
    {
        try
        {
            progress.Report(new(0.0, "Kalibrációs mód indítása..."));
            await _send("CAL_BEGIN");
            await Task.Delay(250, ct);

            // 1) Expozíció: fehér fénynél, kiégés nélkül, majd rögzítés
            progress.Report(new(0.03, "Expozíció beállítása..."));
            await Raw(255, 255, 255);
            await Task.Delay(SettleMs, ct);
            if (!await _cam.AutoExposeAndLockAsync(ct))
                return Fail("A fény túl erős a kamerának (kiég a kép). Menj távolabb, vagy fordítsd el a kamerát a közvetlen fénytől.");

            // 2) Sötét alapkép és fehér referencia -> a mérési terület (a LED fénye által megvilágított cellák)
            progress.Report(new(0.08, "Sötét alapkép..."));
            await Raw(0, 0, 0);
            await Task.Delay(SettleMs + 300, ct);
            var dark = await _cam.GrabAsync(6, ct);

            progress.Report(new(0.11, "Fehér referencia..."));
            await Raw(255, 255, 255);
            await Task.Delay(SettleMs + 300, ct);
            var white = await _cam.GrabAsync(6, ct);

            var mask = BuildMask(white, dark, out string? maskError);
            if (mask == null) return Fail(maskError ?? "A LED fénye nem látszik a kamerán.");
            var darkMean = MaskMean(dark, mask);

            // 3) Csatornánkénti végigfuttatás
            var resp = new double[3][][];
            for (int j = 0; j < 3; j++)
            {
                resp[j] = new double[RgbCalibrator.Points][];
                resp[j][0] = new double[3];
                for (int k = 1; k < RgbCalibrator.Points; k++)
                {
                    ct.ThrowIfCancellationRequested();
                    int d = RgbCalibrator.MeasureDuties[k];
                    await Raw(j == 0 ? d : 0, j == 1 ? d : 0, j == 2 ? d : 0);
                    await Task.Delay(SettleMs, ct);
                    var grid = await _cam.GrabAsync(FramesPerStep, ct);
                    var mean = MaskMean(grid, mask);
                    resp[j][k] = new[] { Math.Max(0, mean[0] - darkMean[0]), Math.Max(0, mean[1] - darkMean[1]), Math.Max(0, mean[2] - darkMean[2]) };
                    double done = (j * (RgbCalibrator.Points - 1) + k) / (3.0 * (RgbCalibrator.Points - 1));
                    progress.Report(new(0.14 + 0.66 * done, $"{Capital(RgbCalibrator.ChannelName(j))} csatorna mérése ({k}/{RgbCalibrator.Points - 1})..."));
                }
            }

            // 4) Táblázat, majd ellenőrzés a valódi fehéren, szükség esetén finomítás
            progress.Report(new(0.82, "Számítás..."));
            var result = RgbCalibrator.Build(resp, null, out string? err);
            if (result == null) return Fail(err ?? "A számítás nem sikerült.");

            var mulTotal = new double[] { 1, 1, 1 };
            double deviation = 0;
            for (int it = 0; it <= MaxRefine; it++)
            {
                ct.ThrowIfCancellationRequested();
                progress.Report(new(0.85 + 0.08 * it / (MaxRefine + 1), $"Ellenőrzés a fehér színen ({it + 1}. kör)..."));
                var wd = RgbCalibrator.WhiteDuties(result);
                await Raw(wd[0], wd[1], wd[2]);
                await Task.Delay(SettleMs + 200, ct);
                var grid = await _cam.GrabAsync(5, ct);
                var m = MaskMean(grid, mask);
                var rgb = new[] { Math.Max(1e-6, m[0] - darkMean[0]), Math.Max(1e-6, m[1] - darkMean[1]), Math.Max(1e-6, m[2] - darkMean[2]) };
                var mul = RgbCalibrator.Refinement(rgb, Tolerance, out deviation);
                if (mul == null || it == MaxRefine) break;
                for (int c = 0; c < 3; c++) mulTotal[c] *= mul[c];
                var next = RgbCalibrator.Build(resp, mulTotal, out _);
                if (next == null) break;
                result = next;
            }

            // 5) Mentés az eszközre és kilépés a kalibrációs módból
            progress.Report(new(0.96, "Mentés az eszközre..."));
            await _send(RgbCalibrator.ToCommand(result));
            await Task.Delay(300, ct);
            await _send("RGBCAL_ON:1");
            progress.Report(new(1.0, "Kész"));
            return new CalibrationOutcome
            {
                Success = true,
                Result = result,
                WhiteDeviation = deviation,
                Message = deviation <= Tolerance
                    ? "A kalibráció kész és elmentve az eszközre."
                    : $"A kalibráció elmentve, de a fehér még kb. {deviation * 100:0}%-ban eltér a semlegestől (mozgott a telefon, vagy változott a fény). Sötétben, nyugodt kézzel megismételheted."
            };
        }
        catch (OperationCanceledException) { return Fail("Megszakítva."); }
        catch (Exception ex) { return Fail("Hiba: " + ex.Message); }
        finally
        {
            try { await _send("CAL_END"); } catch { }
        }
    }

    private static CalibrationOutcome Fail(string message) => new() { Success = false, Message = message };

    private static string Capital(string s) => s.Length == 0 ? s : char.ToUpper(s[0], CultureInfo.InvariantCulture) + s.Substring(1);

    // A mérési terület: az a cellák, amelyeket a LED fénye érezhetően megvilágít (fehér - sötét különbség alapján)
    private static bool[]? BuildMask(float[] white, float[] dark, out string? error)
    {
        error = null;
        var diff = new float[CameraGrid.Cells];
        float max = 0;
        for (int c = 0; c < CameraGrid.Cells; c++)
        {
            float d = 0;
            for (int i = 0; i < 3; i++) d += Math.Max(0, white[c * 3 + i] - dark[c * 3 + i]);
            diff[c] = d;
            if (d > max) max = d;
        }
        if (max < 0.03f)
        {
            error = "A LED fénye nem látszik a kamerán. Irányítsd a kamerát a LED-szalagra (vagy a megvilágított fehér felületre), és sötét helyiségben próbáld.";
            return null;
        }
        var mask = new bool[CameraGrid.Cells];
        int n = 0;
        for (int c = 0; c < CameraGrid.Cells; c++)
            if (diff[c] >= 0.2f * max) { mask[c] = true; n++; }
        if (n < 4)
        {
            error = "A LED fénye túl kis területen látszik. Közelítsd a kamerát a LED-szalaghoz.";
            return null;
        }
        return mask;
    }

    private static double[] MaskMean(float[] grid, bool[] mask)
    {
        double r = 0, g = 0, b = 0; int n = 0;
        for (int c = 0; c < CameraGrid.Cells; c++)
        {
            if (!mask[c]) continue;
            r += grid[c * 3]; g += grid[c * 3 + 1]; b += grid[c * 3 + 2]; n++;
        }
        if (n == 0) return new double[3];
        return new[] { r / n, g / n, b / n };
    }
}
