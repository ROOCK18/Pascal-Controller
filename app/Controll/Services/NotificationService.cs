#if ANDROID
using Android.App;
using Android.Content;
using AndroidX.Core.App;
#endif

namespace Controll.Services;

// Helyi (a telefonon megjelenő) értesítések. Android: értesítési csatorna + engedély (Android 13-tól kell).
public static class NotificationService
{
    public const string AlertChannelId = "pascal_alerts";

    // Az értesítési engedély kérése (Android 13+); más platformon és régebbi Androidon mindig igaz.
    public static async Task<bool> EnsurePermissionAsync()
    {
#if ANDROID
        try
        {
            var status = await MainThread.InvokeOnMainThreadAsync(
                async () => await Permissions.RequestAsync<Permissions.PostNotifications>());
            return status == PermissionStatus.Granted;
        }
        catch { return false; }
#else
        await Task.CompletedTask;
        return true;
#endif
    }

    // Egy értesítés megjelenítése. Azonos "id" felülírja az előzőt (így egy riasztás nem halmozódik fel).
    public static void Show(string title, string text, int id)
    {
#if ANDROID
        try
        {
            var ctx = global::Android.App.Application.Context;
            var nm = ctx.GetSystemService(Context.NotificationService) as NotificationManager;
            if (nm == null) return;

            if (nm.GetNotificationChannel(AlertChannelId) == null)
            {
                var ch = new NotificationChannel(AlertChannelId, "Riasztások", NotificationImportance.High)
                {
                    Description = "Szenzorhiba, túl alacsony vagy túl magas hőmérséklet"
                };
                nm.CreateNotificationChannel(ch);
            }

            PendingIntent? pi = null;
            var launch = ctx.PackageManager?.GetLaunchIntentForPackage(ctx.PackageName ?? "");
            if (launch != null)
                pi = PendingIntent.GetActivity(ctx, 0, launch, PendingIntentFlags.Immutable | PendingIntentFlags.UpdateCurrent);

            var b = new NotificationCompat.Builder(ctx, AlertChannelId);
            b.SetContentTitle(title);
            b.SetContentText(text);
            b.SetStyle(new NotificationCompat.BigTextStyle().BigText(text));
            b.SetSmallIcon(ctx.ApplicationInfo?.Icon ?? 0);
            b.SetAutoCancel(true);
            b.SetPriority(NotificationCompat.PriorityHigh);
            if (pi != null) b.SetContentIntent(pi);

            var n = b.Build();
            if (n != null) nm.Notify(id, n);
        }
        catch (Exception ex) { System.Diagnostics.Debug.WriteLine($"Értesítés: {ex.Message}"); }
#endif
    }
}
