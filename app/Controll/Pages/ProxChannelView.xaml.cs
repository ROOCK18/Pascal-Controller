using System.Globalization;
using Controll.Services;

namespace Controll.Pages;

// Egy csatorna közeledés-beállítása (lásd ProxChannelView.xaml). Két helyen tárolódik a beállítás:
//  - az appban (eszközönként): távolság, újraélesedés (ezt az app méri), és hogy a csatorna be van-e kapcsolva
//  - az eszközön: mit csináljon (érték/művelet) és meddig; az eszköz csak az Automata állapotú csatornát kapcsolja
public partial class ProxChannelView : ContentView
{
    public string ChannelKey { get; set; } = "F1";   // F1 | F2 | R1 | R2 | RGB
    public string Kind { get; set; } = "Fan";        // Fan | Relay | Rgb

    private bool _init;

    public ProxChannelView()
    {
        InitializeComponent();
        Loaded += (s, e) => Init();
    }

    private void Init()
    {
        if (_init) return;
        _init = true;

        bool fan = Kind == "Fan";
        bool rgb = Kind == "Rgb";
        FanValueBox.IsVisible = fan;
        ActionBox.IsVisible = Kind == "Relay";
        RgbBox.IsVisible = rgb;
        LblDur.IsVisible = !rgb;      // az RGB-nek külön felfutási / világítási / lefutási ideje van
        DurEntry.IsVisible = !rgb;
        switch (Kind)
        {
            case "Relay":
                ActionPicker.Items.Add("BE az időtartamra (impulzus)");
                ActionPicker.Items.Add("KI az időtartamra");
                LblDur.Text = "Meddig (mp) - 0: a relé Kapuzás ideje";
                LblIntro.Text = "Amikor a telefon a megadott távolságon belülre ér, a relé végrehajtja ezt (csak Automata állapotban). Az időtartam alatt az automatika nem írja felül.";
                break;
            case "Rgb":
                RgbModePicker.SelectedIndex = 1;
                LblIntro.Text = "Amikor a telefon a megadott távolságon belülre ér, az RGB a beállított színnel, a felfutási idővel elkezd világítani (csak Automata állapotban). Az \"Amíg ott vagyok\" módban a távozást az Újraélesedés X métere adja.";
                break;
            default:
                LblDur.Text = "Meddig (mp) - 0: marad az érték";
                LblIntro.Text = "Amikor a telefon a megadott távolságon belülre ér, a ventilátor erre a fordulatszámra áll (csak Automata állapotban), az időtartam után visszaáll az előző értékre.";
                break;
        }
        if (ActionPicker.Items.Count > 0) ActionPicker.SelectedIndex = 0;

        BluetoothManager.OnDataReceived -= OnData;
        BluetoothManager.OnDataReceived += OnData;
        LoadAppSide();
    }

    // Az oldal megjelenésekor hívandó: az app-oldali beállítások újratöltése
    public void Reload()
    {
        if (!_init) Init(); else LoadAppSide();
    }

    private string? DeviceId => BluetoothManager.GetSavedDeviceId() ?? BluetoothManager.LastUsedId;

    private void LoadAppSide()
    {
        var id = DeviceId;
        var c = id == null ? null : BluetoothManager.FindSaved(id)?.ProxChannels.GetValueOrDefault(ChannelKey);
        c ??= new ProxChannelCfg();
        MetersEntry.Text = c.Meters.ToString("0.#", CultureInfo.InvariantCulture);
        RearmPicker.SelectedIndex = Math.Clamp(c.RearmMode, 0, 2);
        RearmMetersEntry.Text = c.RearmMeters.ToString("0.#", CultureInfo.InvariantCulture);
        RearmSecEntry.Text = c.RearmSec.ToString(CultureInfo.InvariantCulture);
        EnabledSwitch.IsToggled = c.Enabled;
        if (Kind == "Rgb") RgbModePicker.SelectedIndex = Math.Clamp(c.RgbMode, 0, 2);
        UpdateRearmVisibility();
    }

    private void OnRgbModeChanged(object? sender, EventArgs e)
    {
        RgbHoldBox.IsVisible = RgbModePicker.SelectedIndex == 2;
    }

    private void UpdateRearmVisibility()
    {
        int mode = RearmPicker.SelectedIndex;
        RearmMetersBox.IsVisible = mode >= 1;
        RearmSecBox.IsVisible = mode == 2;
    }

    private void OnRearmChanged(object? sender, EventArgs e) => UpdateRearmVisibility();

    // A fejlécre koppintva a legördülő nyílik / záródik
    private void OnHeaderTapped(object? sender, TappedEventArgs e)
    {
        DetailsPanel.IsVisible = !DetailsPanel.IsVisible;
        ExpandIcon.Text = DetailsPanel.IsVisible ? "▲" : "▼";
    }

    // LOAD_PROX:<csatorna>:<be>:<érték>:<időtartam>[:<művelet>] - az eszközön tárolt rész
    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data)) return;
        if (Kind == "Rgb")
        {
            // az RGB közeledése külön parancsot használ (LOAD_RGBPROX); a LOAD_PROX:RGB sort figyelmen kívül hagyjuk
            if (data.StartsWith("LOAD_RGBPROX:")) OnRgbData(data.Substring(13).Trim());
            return;
        }
        if (!data.StartsWith("LOAD_PROX:" + ChannelKey + ":")) return;
        var p = data.Substring(10).Trim().Split(':');
        if (p.Length < 4) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            ValueEntry.Text = p[2];
            DurEntry.Text = p[3];
            if (p.Length >= 5 && ActionPicker.Items.Count > 0) ActionPicker.SelectedIndex = p[4] == "2" ? 1 : 0;
            bool en = p[1] == "1";
            EnabledSwitch.IsToggled = en;
            // az app oldali "be van kapcsolva" jelzés az eszközét követi
            var id = DeviceId;
            if (id != null && BluetoothManager.FindSaved(id) != null)
            {
                BluetoothManager.UpdateSaved(id, sd =>
                {
                    if (!sd.ProxChannels.TryGetValue(ChannelKey, out var cc)) sd.ProxChannels[ChannelKey] = cc = new ProxChannelCfg();
                    cc.Enabled = en;
                });
                ProximityService.Refresh();
                BackgroundHost.Update();
            }
        });
    }

    // LOAD_RGBPROX:<be>:<RRGGBB>:<fényerő>:<felfutás mp>:<mód 0/1/2>:<világítás mp>:<lefutás mp>
    private void OnRgbData(string body)
    {
        var p = body.Split(':');
        if (p.Length < 7) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            bool en = p[0] == "1";
            int mode = int.TryParse(p[4], out int mo) ? Math.Clamp(mo, 0, 2) : 1;
            EnabledSwitch.IsToggled = en;
            RgbColor.Hex = p[1];
            RgbPctEntry.Text = p[2];
            RgbRampUpEntry.Text = p[3];
            RgbModePicker.SelectedIndex = mode;
            RgbHoldEntry.Text = p[5];
            RgbRampDownEntry.Text = p[6];
            var id = DeviceId;
            if (id != null && BluetoothManager.FindSaved(id) != null)
            {
                BluetoothManager.UpdateSaved(id, sd =>
                {
                    if (!sd.ProxChannels.TryGetValue(ChannelKey, out var cc)) sd.ProxChannels[ChannelKey] = cc = new ProxChannelCfg();
                    cc.Enabled = en;
                    cc.RgbMode = mode;
                });
                ProximityService.Refresh();
                BackgroundHost.Update();
            }
        });
    }

    private async void OnSaveTapped(object? sender, TappedEventArgs e)
    {
        var page = Shell.Current.CurrentPage;
        var id = DeviceId;
        if (id == null || !BluetoothManager.IsConnected)
        {
            await page.DisplayAlertAsync("Nincs kapcsolat", "A mentéshez csatlakozz az eszközhöz.", "OK");
            return;
        }

        bool enabled = EnabledSwitch.IsToggled;
        if (!double.TryParse((MetersEntry.Text ?? "").Trim().Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out double meters)
            || meters < 1 || meters > 500)
        {
            await page.DisplayAlertAsync("Hibás érték", "A távolság 1 és 500 méter közötti szám legyen.", "OK");
            return;
        }
        int value = 100;
        if (Kind == "Fan" && (!int.TryParse((ValueEntry.Text ?? "").Trim(), out value) || value < 0 || value > 100))
        {
            await page.DisplayAlertAsync("Hibás érték", "A fordulatszám 0 és 100 közötti szám legyen.", "OK");
            return;
        }
        int seconds = 0;
        if (Kind != "Rgb" && (!int.TryParse((DurEntry.Text ?? "").Trim(), out seconds) || seconds < 0 || seconds > 65535))
        {
            await page.DisplayAlertAsync("Hibás érték", "Az időtartam 0 és 65535 másodperc közötti szám legyen.", "OK");
            return;
        }
        int rearmMode = Math.Clamp(RearmPicker.SelectedIndex, 0, 2);
        double rearmMeters = 15; int rearmSec = 30;
        if (rearmMode >= 1
            && (!double.TryParse((RearmMetersEntry.Text ?? "").Trim().Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out rearmMeters)
                || rearmMeters <= meters || rearmMeters > 1000))
        {
            await page.DisplayAlertAsync("Hibás érték", "Az újraélesedés távolsága nagyobb legyen a végrehajtási távolságnál (legfeljebb 1000 m).", "OK");
            return;
        }
        if (rearmMode == 2 && (!int.TryParse((RearmSecEntry.Text ?? "").Trim(), out rearmSec) || rearmSec < 1 || rearmSec > 86400))
        {
            await page.DisplayAlertAsync("Hibás érték", "Az idő 1 és 86400 másodperc közötti szám legyen.", "OK");
            return;
        }
        int action = (Kind == "Relay" && ActionPicker.SelectedIndex == 1) ? 2 : 0;

        // RGB: szín, fényerő, felfutás, világítási mód, világítási idő, lefutás
        int rgbPct = 100, rgbUp = 0, rgbHold = 0, rgbDown = 0;
        int rgbMode = Math.Clamp(RgbModePicker.SelectedIndex, 0, 2);
        if (Kind == "Rgb")
        {
            if (!int.TryParse((RgbPctEntry.Text ?? "").Trim(), out rgbPct) || rgbPct < 0 || rgbPct > 100)
            {
                await page.DisplayAlertAsync("Hibás érték", "A fényerő 0 és 100 közötti szám legyen.", "OK");
                return;
            }
            if (!int.TryParse((RgbRampUpEntry.Text ?? "").Trim(), out rgbUp) || rgbUp < 0 || rgbUp > 3600
                || !int.TryParse((RgbRampDownEntry.Text ?? "").Trim(), out rgbDown) || rgbDown < 0 || rgbDown > 3600)
            {
                await page.DisplayAlertAsync("Hibás érték", "A felfutási és a lefutási idő 0 és 3600 másodperc közötti szám legyen.", "OK");
                return;
            }
            if (rgbMode == 2 && (!int.TryParse((RgbHoldEntry.Text ?? "").Trim(), out rgbHold) || rgbHold < 0 || rgbHold > 65535))
            {
                await page.DisplayAlertAsync("Hibás érték", "A világítási idő 0 és 65535 másodperc közötti szám legyen.", "OK");
                return;
            }
            if (rgbMode != 2 && int.TryParse((RgbHoldEntry.Text ?? "").Trim(), out int h0)) rgbHold = Math.Clamp(h0, 0, 65535);
        }

        if (enabled) await NotificationService.EnsurePermissionAsync();

        BluetoothManager.UpdateSaved(id, sd => sd.ProxChannels[ChannelKey] = new ProxChannelCfg
        {
            Enabled = enabled, Meters = meters, RearmMode = rearmMode, RearmMeters = rearmMeters, RearmSec = rearmSec, RgbMode = rgbMode
        });
        if (Kind == "Rgb")
            await BluetoothManager.WriteAsync($"SET_RGBPROX:{(enabled ? 1 : 0)}:{RgbColor.Hex}:{rgbPct}:{rgbUp}:{rgbMode}:{rgbHold}:{rgbDown}");
        else
            await BluetoothManager.WriteAsync($"SET_PROX:{ChannelKey}:{(enabled ? 1 : 0)}:{value}:{seconds}:{action}");

        ProximityService.Refresh();
        BackgroundHost.Update();
        BluetoothManager.StartAutoReconnect();
        await page.DisplayAlertAsync("Siker", "Közeledés beállítás elmentve.", "OK");
    }
}
