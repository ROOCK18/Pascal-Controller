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
        _ = WarnIfBluetoothOffAsync();
    }

    protected override void OnResume()
    {
        base.OnResume();
        BluetoothManager.StartAutoReconnect();
        _ = WarnIfBluetoothOffAsync();
    }

    private static bool _btWarnShowing;

    // Ha a telefon Bluetooth-ja ki van kapcsolva, nem találja meg az eszközt: erre figyelmeztet (induláskor és visszatéréskor).
    private static async Task WarnIfBluetoothOffAsync()
    {
        if (_btWarnShowing) return;
        try
        {
            await Task.Delay(1200);   // várjuk meg, míg az első oldal megjelenik
            var state = Plugin.BLE.CrossBluetoothLE.Current.State;
            if (state != Plugin.BLE.Abstractions.Contracts.BluetoothState.Off &&
                state != Plugin.BLE.Abstractions.Contracts.BluetoothState.TurningOff) return;
            _btWarnShowing = true;
            await MainThread.InvokeOnMainThreadAsync(async () =>
            {
                var page = Application.Current?.Windows.FirstOrDefault()?.Page;
                if (page == null) return;
                bool open = await page.DisplayAlertAsync("Bluetooth ki van kapcsolva",
                    "A telefon Bluetooth-ja ki van kapcsolva, ezért az eszköz nem érhető el. Kapcsold be, majd az app magától csatlakozik.",
                    "Beállítások", "Később");
#if ANDROID
                if (open)
                {
                    var intent = new Android.Content.Intent(Android.Provider.Settings.ActionBluetoothSettings);
                    intent.AddFlags(Android.Content.ActivityFlags.NewTask);
                    Android.App.Application.Context.StartActivity(intent);
                }
#else
                _ = open;
#endif
            });
        }
        catch (Exception ex) { System.Diagnostics.Debug.WriteLine($"BT figyelmeztetés: {ex.Message}"); }
        finally { _btWarnShowing = false; }
    }
}
