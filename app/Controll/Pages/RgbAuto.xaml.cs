using Controll.Services;

namespace Controll.Pages;

// RGB automatikák oldal (az RGB oldal "RGB automatikák" gombjáról nyílik): egyelőre egy lenyíló, a közeledés.
public partial class RgbAuto : ContentView
{
    public RgbAuto()
    {
        InitializeComponent();
    }

    // Megnyitáskor hívja a MainPage: betölti a beállításokat az appból és az eszközről
    public async void RequestProx()
    {
        ProxRgbView.Reload();
        await Task.Delay(300);
        if (BluetoothManager.IsConnected) await BluetoothManager.WriteAsync("GET_PROX");
    }

    private void OnBackClicked(object? sender, TappedEventArgs e)
    {
        if (Shell.Current.CurrentPage is MainPage main) main.ShowRgbFromAuto();
    }
}
