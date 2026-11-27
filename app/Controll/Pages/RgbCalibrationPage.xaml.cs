using System.Globalization;
using Controll.Services;
#if ANDROID
using AndroidX.Camera.View;
using AndroidX.Lifecycle;
#endif

namespace Controll.Pages;

// Színkalibrálás: a kamera a LED fényét méri, az app BLE-n vezérli az eszközt, az eredmény az eszközben (NVS) tárolódik.
// A teljes menet automatikus, egyetlen gombnyomás (RgbCalibrationRunner).
public partial class RgbCalibrationPage : ContentPage
{
    private ICameraMeter? _meter;
#if ANDROID
    private CameraXMeter? _android;
#endif
    private CancellationTokenSource? _cts;
    private bool _running;
    private bool _loadingSwitch;

    public RgbCalibrationPage()
    {
        InitializeComponent();
        BluetoothManager.OnDataReceived += OnData;
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        await StartCameraAsync();
        await BluetoothManager.WriteAsync("GET_RGBCAL");
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        _cts?.Cancel();
        StopCamera();
        BluetoothManager.OnDataReceived -= OnData;
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
            StartButton.IsEnabled = BluetoothManager.IsConnected;
#else
            StatusLabel.Text = "A kalibrálás csak Androidon érhető el.";
#endif
        }
        catch (Exception ex) { StatusLabel.Text = "Kamera hiba: " + ex.Message; }
    }

    private void StopCamera()
    {
#if ANDROID
        _android?.Stop();
        _android = null;
#endif
        _meter = null;
    }

    private async void OnStartClicked(object? sender, EventArgs e)
    {
        if (_running || _meter == null) return;
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        _running = true;
        StartButton.IsEnabled = false;
        ResultLabel.Text = "";
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
                ResultLabel.Text = Summary(outcome.Result);
                await BluetoothManager.WriteAsync("GET_RGBCAL");
            }
        }
        finally
        {
            DeviceDisplay.Current.KeepScreenOn = false;
            Progress.IsVisible = false;
            _running = false;
            StartButton.IsEnabled = _meter != null && BluetoothManager.IsConnected;
        }
    }

    private static string Summary(RgbCalibrationResult r)
    {
        var sb = new System.Text.StringBuilder();
        for (int c = 0; c < 3; c++)
        {
            string name = RgbCalibrator.ChannelName(c);
            name = char.ToUpper(name[0], CultureInfo.InvariantCulture) + name.Substring(1);
            sb.Append(name).Append(": nyitás kb. ").Append(r.Onset[c]).Append(" / 255, teljes fény ").Append((r.Gain[c] * 100).ToString("0", CultureInfo.InvariantCulture)).AppendLine(" %");
        }
        return sb.ToString().TrimEnd();
    }

    // ── Az eszköz állapota (LOAD_RGBCAL:<használatban 0/1>:<van mentett kalibráció 0/1>) ──
    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data) || !data.StartsWith("LOAD_RGBCAL:")) return;
        var t = data.Substring("LOAD_RGBCAL:".Length).Trim().Split(':');
        if (t.Length < 2) return;
        bool on = t[0] == "1", has = t[1] == "1";
        MainThread.BeginInvokeOnMainThread(() =>
        {
            _loadingSwitch = true;
            UseSwitch.IsToggled = on;
            _loadingSwitch = false;
            StoredLabel.Text = has ? "Az eszközön: mentett kalibráció van" : "Az eszközön: nincs kalibráció";
        });
    }

    private async void OnUseToggled(object? sender, ToggledEventArgs e)
    {
        if (_loadingSwitch) return;
        await BluetoothManager.WriteAsync(e.Value ? "RGBCAL_ON:1" : "RGBCAL_ON:0");
    }

    private async void OnResetClicked(object? sender, EventArgs e)
    {
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        bool ok = await DisplayAlertAsync("Kalibráció törlése", "Biztosan törlöd az eszközön tárolt kalibrációt? A színek újra nyers kitöltéssel jelennek meg.", "Törlés", "Mégse");
        if (!ok) return;
        await BluetoothManager.WriteAsync("RGBCAL_RESET");
        await Task.Delay(200);
        await BluetoothManager.WriteAsync("GET_RGBCAL");
        ResultLabel.Text = "";
    }
}
