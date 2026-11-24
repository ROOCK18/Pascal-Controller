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
        FanValueBox.IsVisible = fan;
        ActionBox.IsVisible = !fan;
        switch (Kind)
        {
            case "Relay":
                ActionPicker.Items.Add("BE az időtartamra (impulzus)");
                ActionPicker.Items.Add("KI az időtartamra");
                LblDur.Text = "Meddig (mp) - 0: a relé Kapuzás ideje";
                LblIntro.Text = "Amikor a telefon a megadott távolságon belülre ér, a relé végrehajtja ezt (csak Automata állapotban). Az időtartam alatt az automatika nem írja felül.";
                break;
            case "Rgb":
                ActionPicker.Items.Add("BE");
                ActionPicker.Items.Add("KI");
                LblDur.Text = "Meddig (mp) - 0: marad";
                LblIntro.Text = "Amikor a telefon a megadott távolságon belülre ér, az RGB ezt csinálja. Az időtartam után az ellenkezőjére áll (0 mp: marad).";
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

        // élő állapot kijelzése (2 mp-enként, amíg a nézet látszik)
        var timer = Dispatcher.CreateTimer();
        timer.Interval = TimeSpan.FromSeconds(2);
        timer.Tick += (s, e) => { if (IsVisible && EnabledSwitch.IsToggled) LblStatus.Text = ProximityService.Describe(ChannelKey); };
        timer.Start();
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
        DetailsPanel.IsVisible = c.Enabled;
        UpdateRearmVisibility();
    }

    private void UpdateRearmVisibility()
    {
        int mode = RearmPicker.SelectedIndex;
        RearmMetersBox.IsVisible = mode >= 1;
        RearmSecBox.IsVisible = mode == 2;
    }

    private void OnRearmChanged(object? sender, EventArgs e) => UpdateRearmVisibility();

    // A kapcsoló bekapcsolásakor tovább nyílik a beállítás
    private void OnToggled(object? sender, ToggledEventArgs e)
    {
        DetailsPanel.IsVisible = e.Value;
    }

    // LOAD_PROX:<csatorna>:<be>:<érték>:<időtartam>[:<művelet>] - az eszközön tárolt rész
    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data) || !data.StartsWith("LOAD_PROX:" + ChannelKey + ":")) return;
        var p = data.Substring(10).Trim().Split(':');
        if (p.Length < 4) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            ValueEntry.Text = p[2];
            DurEntry.Text = p[3];
            if (p.Length >= 5 && ActionPicker.Items.Count > 0) ActionPicker.SelectedIndex = p[4] == "2" ? 1 : 0;
            bool en = p[1] == "1";
            EnabledSwitch.IsToggled = en;
            DetailsPanel.IsVisible = en;
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
        if (!int.TryParse((DurEntry.Text ?? "").Trim(), out int seconds) || seconds < 0 || seconds > 65535)
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
        int action = (Kind != "Fan" && ActionPicker.SelectedIndex == 1) ? 2 : 0;

        if (enabled) await NotificationService.EnsurePermissionAsync();

        BluetoothManager.UpdateSaved(id, sd => sd.ProxChannels[ChannelKey] = new ProxChannelCfg
        {
            Enabled = enabled, Meters = meters, RearmMode = rearmMode, RearmMeters = rearmMeters, RearmSec = rearmSec
        });
        await BluetoothManager.WriteAsync($"SET_PROX:{ChannelKey}:{(enabled ? 1 : 0)}:{value}:{seconds}:{action}");

        ProximityService.Refresh();
        BackgroundHost.Update();
        BluetoothManager.StartAutoReconnect();
        await page.DisplayAlertAsync("Siker", "Közeledés beállítás elmentve.", "OK");
    }
}
