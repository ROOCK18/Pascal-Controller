using Controll.Services;

namespace Controll.Pages;

// RGB Automata / Kézi kapcsoló. Automata állapotban az RGB automatikák (időzítő, görbék, közeledés) vezérelnek,
// Kézi állapotban egyik sem. Az állapot az eszközön tárolódik (SET_AUTO:RGB:<0/1>, LOAD_RGBAUTO:<0/1>).
// Több példány is lehet (RGB oldal, RGB automatikák oldal): a közös állapotot egy statikus érték tartja szinkronban.
public partial class RgbAutoToggle : ContentView
{
    private static bool _auto;
    private static event Action? StateChanged;
    private bool _init;

    public RgbAutoToggle()
    {
        InitializeComponent();
        Loaded += (s, e) => Init();
    }

    // Az RGB automatikák az egész oldalon csak Automata állapotban működnek
    public static bool IsAuto => _auto;

    // Az a kézi vezérlő-blokk, amit Automata állapotban le kell tiltani (az RGB oldalon). Az eszköz is eldobja a kézi parancsokat.
    public View? ManualControls { get; set; }

    private void Init()
    {
        if (_init) return;
        _init = true;
        BluetoothManager.OnDataReceived += OnData;
        BluetoothManager.OnConnectionChanged += OnConnection;
        StateChanged += UpdateVisual;
        UpdateVisual();
        if (BluetoothManager.IsConnected) RequestState();
    }

    private static async void RequestState()
    {
        try { await BluetoothManager.WriteAsync("GET_RGBAUTO"); } catch { }
    }

    private void OnConnection(bool connected)
    {
        if (connected) RequestState();
    }

    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data) || !data.StartsWith("LOAD_RGBAUTO:")) return;
        bool a = data.Substring(13).Trim() == "1";
        MainThread.BeginInvokeOnMainThread(() => { _auto = a; StateChanged?.Invoke(); });
    }

    private void UpdateVisual()
    {
        AutoBorder.BackgroundColor = _auto ? UiHelper.ThemeColor("ControlGray") : UiHelper.ThemeColor("PrimaryRed");
        AutoLabel.Text = _auto ? "Automata" : "Kézi";
        if (ManualControls != null)
        {
            ManualControls.IsEnabled = !_auto;
            ManualControls.InputTransparent = _auto;
            ManualControls.Opacity = _auto ? 0.4 : 1.0;
        }
    }

    private async void OnTapped(object? sender, TappedEventArgs e)
    {
        _auto = !_auto;
        StateChanged?.Invoke();
        await BluetoothManager.WriteAsync($"SET_AUTO:RGB:{(_auto ? 1 : 0)}");
    }
}
