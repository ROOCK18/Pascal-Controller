using System.Globalization;
using Controll.Services;

namespace Controll.Pages;

// Színkalibrálás (kézi): a LED-ek fehéren világítanak a kalibrációval, a három csúszka (piros, zöld, kék) a csatornák
// fényerejét (%) állítja élőben; a MENTÉS az eszközbe tárolja. A "Kalibrálás kamerával" az automatikus mérést indítja.
// Az eszköz oldalán a REFWHITE mód nem perzisztens: mentés nélkül kilépve a régi kalibráció marad.
public partial class RgbCalibrationPage : ContentPage
{
    private readonly byte[][] _base = { RgbCalibrator.IdentityLut(), RgbCalibrator.IdentityLut(), RgbCalibrator.IdentityLut() };
    private readonly bool[] _got = new bool[3];
    private bool _loading;
    private IDispatcherTimer? _keepAlive;
    private CancellationTokenSource? _debounce;
    private readonly Slider[] _sl;
    private readonly Entry[] _en;

    public RgbCalibrationPage()
    {
        InitializeComponent();
        _sl = new[] { SliderR, SliderG, SliderB };
        _en = new[] { EntryR, EntryG, EntryB };
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        BluetoothManager.OnDataReceived += OnData;
        Array.Clear(_got);
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        StatusLabel.Text = "";
        await BluetoothManager.WriteAsync("REFWHITE:1");
        await BluetoothManager.WriteAsync("GET_RGBCAL");

        // Az eszköz 2 perc után magától kilép a referencia-fehérből: ezért megújítjuk
        _keepAlive = Dispatcher.CreateTimer();
        _keepAlive.Interval = TimeSpan.FromSeconds(30);
        _keepAlive.Tick += async (s, e) => await BluetoothManager.WriteAsync("REFWHITE:1");
        _keepAlive.Start();
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        _keepAlive?.Stop();
        _keepAlive = null;
        _debounce?.Cancel();
        BluetoothManager.OnDataReceived -= OnData;
        if (BluetoothManager.IsConnected) _ = BluetoothManager.WriteAsync("REFWHITE:0");
    }

    // ── Eszközről érkező adatok ──
    //   LOAD_RGBCAL:<használatban 0/1>:<van mentett 0/1>      LOAD_RGBCALTAB:<csatorna 0..2>:<17 érték, vesszővel>
    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data)) return;
        if (data.StartsWith("LOAD_RGBCAL:"))
        {
            var t = data.Substring("LOAD_RGBCAL:".Length).Trim().Split(':');
            if (t.Length < 2) return;
            bool on = t[0] == "1", has = t[1] == "1";
            MainThread.BeginInvokeOnMainThread(() =>
            {
                _loading = true;
                UseSwitch.IsToggled = on;
                if (!has)
                {
                    for (int c = 0; c < 3; c++) _base[c] = RgbCalibrator.IdentityLut();
                    SetAll(new[] { 100, 100, 100 });
                }
                _loading = false;
            });
        }
        else if (data.StartsWith("LOAD_RGBCALTAB:"))
        {
            var t = data.Substring("LOAD_RGBCALTAB:".Length).Trim().Split(':');
            if (t.Length < 2 || !int.TryParse(t[0], out int ch) || ch < 0 || ch > 2) return;
            var vals = t[1].Split(',');
            if (vals.Length != RgbCalibrator.LutPoints) return;
            var lut = new byte[RgbCalibrator.LutPoints];
            for (int k = 0; k < lut.Length; k++)
            {
                if (!int.TryParse(vals[k], NumberStyles.Integer, CultureInfo.InvariantCulture, out int v)) return;
                lut[k] = (byte)Math.Clamp(v, 0, 255);
            }
            MainThread.BeginInvokeOnMainThread(() =>
            {
                _base[ch] = lut;
                _got[ch] = true;
                if (_got.All(g => g))
                    SetAll(_base.Select(b => (int)Math.Round(RgbCalibrator.TopPercent(b))).ToArray());
            });
        }
    }

    // A csúszkák és a számmezők beállítása (visszahatás nélkül)
    private void SetAll(int[] pct)
    {
        bool prev = _loading;
        _loading = true;
        for (int c = 0; c < 3; c++)
        {
            _sl[c].Value = pct[c];
            _en[c].Text = pct[c].ToString(CultureInfo.InvariantCulture);
        }
        _loading = prev;
    }

    private int Pct(int c) => (int)Math.Round(_sl[c].Value);

    // ── Csúszka / számmező ──
    private void OnSliderChanged(object? sender, ValueChangedEventArgs e)
    {
        if (_loading) return;
        int c = Array.IndexOf(_sl, sender as Slider);
        if (c < 0) return;
        _loading = true;
        _en[c].Text = Pct(c).ToString(CultureInfo.InvariantCulture);
        _loading = false;
        ScheduleSend();
    }

    private void OnEntryChanged(object? sender, TextChangedEventArgs e)
    {
        if (_loading) return;
        int c = Array.IndexOf(_en, sender as Entry);
        if (c < 0 || !int.TryParse(e.NewTextValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out int v)) return;
        v = Math.Clamp(v, 0, 100);
        _loading = true;
        _sl[c].Value = v;
        if (e.NewTextValue != v.ToString(CultureInfo.InvariantCulture)) _en[c].Text = v.ToString(CultureInfo.InvariantCulture);
        _loading = false;
        ScheduleSend();
    }

    // A változásokat 150 ms-onként összevonjuk, hogy ne árasszuk el a BLE-t
    private async void ScheduleSend()
    {
        _debounce?.Cancel();
        var cts = _debounce = new CancellationTokenSource();
        try
        {
            await Task.Delay(150, cts.Token);
            var d = new int[3];
            for (int c = 0; c < 3; c++) d[c] = RgbCalibrator.ScaleLut(_base[c], Pct(c))[RgbCalibrator.LutPoints - 1];
            await BluetoothManager.WriteAsync($"REFW:{d[0]}:{d[1]}:{d[2]}");
        }
        catch (OperationCanceledException) { }
    }

    // ── Gombok ──
    private async void OnSaveClicked(object? sender, EventArgs e)
    {
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        _debounce?.Cancel();
        var lut = new byte[3][];
        for (int c = 0; c < 3; c++) lut[c] = RgbCalibrator.ScaleLut(_base[c], Pct(c));
        await BluetoothManager.WriteAsync(RgbCalibrator.ToCommand(lut));
        await BluetoothManager.WriteAsync("RGBCAL_ON:1");
        for (int c = 0; c < 3; c++) _base[c] = lut[c];
        StatusLabel.Text = "Mentve.";
    }

    private async void OnCameraClicked(object? sender, EventArgs e)
    {
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        await Navigation.PushAsync(new RgbCameraCalibrationPage());
    }

    private async void OnUseToggled(object? sender, ToggledEventArgs e)
    {
        if (_loading) return;
        await BluetoothManager.WriteAsync(e.Value ? "RGBCAL_ON:1" : "RGBCAL_ON:0");
    }

    private async void OnResetClicked(object? sender, EventArgs e)
    {
        if (!BluetoothManager.IsConnected) { StatusLabel.Text = "Nincs csatlakozva az eszközhöz."; return; }
        bool ok = await DisplayAlertAsync("Törlés", "Törlöd a kalibrációt? Minden csatorna 100 % lesz.", "Törlés", "Mégse");
        if (!ok) return;
        _debounce?.Cancel();
        await BluetoothManager.WriteAsync("RGBCAL_RESET");
        for (int c = 0; c < 3; c++) _base[c] = RgbCalibrator.IdentityLut();
        SetAll(new[] { 100, 100, 100 });
        await BluetoothManager.WriteAsync("REFW:255:255:255");
        StatusLabel.Text = "Törölve.";
    }
}
