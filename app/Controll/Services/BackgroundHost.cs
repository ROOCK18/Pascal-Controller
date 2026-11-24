namespace Controll.Services;

// A háttérben futás kezelése: az Android "előtér-szolgáltatás" (állandó értesítéssel) tartja életben az appot,
// hogy a közeledés-érzékelés és a riasztások a képernyő kikapcsolt állapotában / az app bezárása után is működjenek.
public static class BackgroundHost
{
    // Háttérfigyelés a riasztásokhoz (az Értesítések panelen állítható)
    public static bool MonitorAlerts
    {
        get => Preferences.Get("BgMonitor", false);
        set => Preferences.Set("BgMonitor", value);
    }

    public static bool Needed => MonitorAlerts || ProximityService.AnyEnabled;

    // Beállítás-változás / indulás után hívandó: elindítja vagy leállítja a szolgáltatást.
    public static void Update()
    {
#if ANDROID
        try
        {
            var ctx = global::Android.App.Application.Context;
            var intent = new global::Android.Content.Intent(ctx, typeof(PascalForegroundService));
            if (Needed) ctx.StartForegroundService(intent);
            else ctx.StopService(intent);
        }
        catch (Exception ex) { System.Diagnostics.Debug.WriteLine($"Háttérszolgáltatás: {ex.Message}"); }
#endif
    }
}
