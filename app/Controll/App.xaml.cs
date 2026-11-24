using Controll.Services;

namespace Controll;

public partial class App : Application
{
    public App()
    {
        InitializeComponent();
    }

    protected override Window CreateWindow(IActivationState? activationState)
        => new Window(new AppShell());

    // Induláskor és az előtérbe kerüléskor megpróbál újra csatlakozni a legutóbbi eszközhöz.
    protected override void OnStart()
    {
        base.OnStart();
        BluetoothManager.Initialize(Plugin.BLE.CrossBluetoothLE.Current.Adapter);
        AlertService.Initialize();          // riasztások -> telefonos értesítés
        ProximityService.Refresh();         // közeledés érzékelés (ha be van állítva)
        BackgroundHost.Update();            // háttérszolgáltatás (közeledés / riasztás-figyelés)
        BluetoothManager.StartAutoReconnect(allowPicker: true);
    }

    protected override void OnResume()
    {
        base.OnResume();
        BluetoothManager.StartAutoReconnect();
    }
}
