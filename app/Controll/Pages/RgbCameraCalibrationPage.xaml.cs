using Controll.Services;
#if ANDROID
using AndroidX.Camera.View;
using AndroidX.Lifecycle;
#endif

namespace Controll.Pages;

// Színkalibrálás: a kamera a LED fényét méri, az app BLE-n vezérli az eszközt, az eredmény az eszközben (NVS) tárolódik.
// A teljes menet automatikus, egyetlen gombnyomás (RgbCalibrationRunner).
public partial class RgbCameraCalibrationPage : ContentPage
{
    private ICameraMeter? _meter;
#if ANDROID
    private CameraXMeter? _android;
#endif
    private CancellationTokenSource? _cts;
    private bool _running;

    public RgbCameraCalibrationPage()
    {
        InitializeComponent();
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        await StartCameraAsync();
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        _cts?.Cancel();
        StopCamera();
    }

    private async Task StartCameraAsync()
    {
        try
        {
            var perm = await Permissions.CheckStatusAsync<Permissions.Camera>();
            if (perm != PermissionStatus.Granted) perm = await Permissions.RequestAsync<Permissions.Camera>();
            if (perm != PermissionStatus.Granted)
            {
                StatusLabel.Text = "A kalibráláshoz kamera-engedély kell (Beállítások > Alkalmazások > Controll > Engedélyek).";
                return;
            }
#if ANDROID
            await Task.Delay(300);   // az előnézet natív nézete a megjelenés után készül el
            var activity = Platform.CurrentActivity;
            var pv = Preview.Handler?.PlatformView as PreviewView;
            if (activity == null || pv == null || activity is not ILifecycleOwner owner)
            {
                StatusLabel.Text = "A kamera nem indítható.";
                return;
            }
            _android = await CameraXMeter.StartAsync(activity, pv, owner);
            _meter = _android;
            StatusLabel.Text = BluetoothManager.IsConnected ? "Kész. Irányítsd a LED-re, majd nyomd meg az Indítást." : "Nincs csatlakozva az eszközhöz.";
            SetStartEnabled(BluetoothManager.IsConnected);
#else
            StatusLabel.Text = "A kalibrálás csak Androidon érhető el.";
#endif
        }
        catch (Exception ex) { StatusLabel.Text = "Kamera hiba: " + ex.Message; }
    }

    private void SetStartEnabled(bool on)
    {
        StartButton.IsEnabled = on;
        StartButton.Opacity = on ? 1.0 : 0.4;
    }

    private void StopCamera()
    {
#if ANDROID
        _android?.Stop();
        _android = null;
#endif
        _meter = null;
    }

    private async void OnStartTapped(object? sender, TappedEventArgs e)
    {
        if (_running || _meter == null) return;
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        _running = true;
        SetStartEnabled(false);
        Progress.IsVisible = true;
        Progress.Progress = 0;
        DeviceDisplay.Current.KeepScreenOn = true;
        _cts = new CancellationTokenSource();
        try
        {
            var runner = new RgbCalibrationRunner(_meter, BluetoothManager.WriteAsync);
            var progress = new Progress<CalibrationProgress>(p =>
            {
                Progress.Progress = p.Fraction;
                StatusLabel.Text = p.Text;
            });
            var outcome = await runner.RunAsync(progress, _cts.Token);
            StatusLabel.Text = outcome.Message;
            if (outcome.Success && outcome.Result != null)
            {
            }
        }
        finally
        {
            DeviceDisplay.Current.KeepScreenOn = false;
            Progress.IsVisible = false;
            _running = false;
            SetStartEnabled(_meter != null && BluetoothManager.IsConnected);
        }
    }
}
