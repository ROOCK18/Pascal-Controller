using System.Globalization;
using Controll.Services;

namespace Controll.Pages;

// Egy csatorna figyelmeztetései (lásd AlertChannelView.xaml). A beállítás az eszközön tárolódik:
// SET_CALERT:<csatorna>:<be>:<szenzorhiba>:<alsó be>:<alsó °C>:<felső be>:<felső °C>
public partial class AlertChannelView : ContentView
{
    public string ChannelKey { get; set; } = "F1";   // F1 | F2 | R1 | R2

    private bool _init;

    public AlertChannelView()
    {
        InitializeComponent();
        Loaded += (s, e) => Init();
    }

    private void Init()
    {
        if (_init) return;
        _init = true;
        BluetoothManager.OnDataReceived -= OnData;
        BluetoothManager.OnDataReceived += OnData;
    }

    private void OnHeaderTapped(object? sender, TappedEventArgs e)
    {
        DetailsPanel.IsVisible = !DetailsPanel.IsVisible;
        ExpandIcon.Text = DetailsPanel.IsVisible ? "▲" : "▼";
    }

    // LOAD_CALERT:<csatorna>:<be>:<szenzorhiba>:<alsó be>:<alsó>:<felső be>:<felső>
    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data) || !data.StartsWith("LOAD_CALERT:" + ChannelKey + ":")) return;
        var p = data.Substring(12).Trim().Split(':');
        if (p.Length < 7) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            EnabledSwitch.IsToggled = p[1] == "1";
            FaultSwitch.IsToggled = p[2] == "1";
            LowSwitch.IsToggled = p[3] == "1";
            LowEntry.Text = p[4];
            HighSwitch.IsToggled = p[5] == "1";
            HighEntry.Text = p[6];
        });
    }

    private static string F(double v) => v.ToString("0.#", CultureInfo.InvariantCulture);

    private async void OnSaveTapped(object? sender, TappedEventArgs e)
    {
        var page = Shell.Current.CurrentPage;
        if (!BluetoothManager.IsConnected)
        {
            await page.DisplayAlertAsync("Nincs kapcsolat", "A mentéshez csatlakozz az eszközhöz.", "OK");
            return;
        }
        if (!double.TryParse((LowEntry.Text ?? "").Trim().Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out double low)
            || low < -50 || low > 150
            || !double.TryParse((HighEntry.Text ?? "").Trim().Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out double high)
            || high < -50 || high > 150)
        {
            await page.DisplayAlertAsync("Hibás érték", "A határok -50 és 150 °C közötti számok legyenek.", "OK");
            return;
        }
        if (LowSwitch.IsToggled && HighSwitch.IsToggled && low >= high)
        {
            await page.DisplayAlertAsync("Hibás érték", "Az alsó határ kisebb legyen a felsőnél.", "OK");
            return;
        }

        if (EnabledSwitch.IsToggled) await NotificationService.EnsurePermissionAsync();
        await BluetoothManager.WriteAsync(
            $"SET_CALERT:{ChannelKey}:{(EnabledSwitch.IsToggled ? 1 : 0)}:{(FaultSwitch.IsToggled ? 1 : 0)}:" +
            $"{(LowSwitch.IsToggled ? 1 : 0)}:{F(low)}:{(HighSwitch.IsToggled ? 1 : 0)}:{F(high)}");
        await page.DisplayAlertAsync("Siker", "Figyelmeztetés beállítás elmentve.", "OK");
    }
}
