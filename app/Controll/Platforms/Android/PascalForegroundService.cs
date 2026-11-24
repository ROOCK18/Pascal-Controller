using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using AndroidX.Core.App;

namespace Controll
{
    // Előtér-szolgáltatás: az állandó értesítés miatt az Android nem állítja le az appot a háttérben,
    // így a Bluetooth-kapcsolat, a közeledés-érzékelés és a riasztások tovább működnek.
    [Service(Exported = false, ForegroundServiceType = ForegroundService.TypeConnectedDevice)]
    public class PascalForegroundService : Service
    {
        // Új csatorna-azonosító: egy már létrehozott csatorna fontosságát az Android nem engedi utólag csökkenteni
        private const string ChannelId = "pascal_bg_min";
        private const string OldChannelId = "pascal_background";
        private const int NotificationId = 1001;

        public override IBinder? OnBind(Intent? intent) => null;

        public override StartCommandResult OnStartCommand(Intent? intent, StartCommandFlags flags, int startId)
        {
            var nm = GetSystemService(NotificationService) as NotificationManager;
            if (nm != null)
            {
                if (nm.GetNotificationChannel(OldChannelId) != null) nm.DeleteNotificationChannel(OldChannelId);
                if (nm.GetNotificationChannel(ChannelId) == null)
                {
                    // Minimális fontosság: néma, nem jelenik meg az állapotsorban, csak az értesítési panel alján
                    var ch = new NotificationChannel(ChannelId, "Háttérkapcsolat", NotificationImportance.Min)
                    {
                        Description = "Az Androidnak kötelező jelzés, hogy az app a háttérben is tartja a kapcsolatot"
                    };
                    ch.SetShowBadge(false);
                    nm.CreateNotificationChannel(ch);
                }
            }

            PendingIntent? pi = null;
            var launch = PackageManager?.GetLaunchIntentForPackage(PackageName ?? "");
            if (launch != null)
                pi = PendingIntent.GetActivity(this, 0, launch, PendingIntentFlags.Immutable | PendingIntentFlags.UpdateCurrent);

            var b = new NotificationCompat.Builder(this, ChannelId);
            b.SetContentTitle("Pascal-Controller");
            b.SetSmallIcon(ApplicationInfo?.Icon ?? 0);
            b.SetOngoing(true);
            b.SetPriority(NotificationCompat.PriorityMin);
            b.SetVisibility(NotificationCompat.VisibilitySecret);   // zárolt képernyőn nem látszik
            b.SetShowWhen(false);
            if (pi != null) b.SetContentIntent(pi);
            var n = b.Build();

            if (n != null)
            {
                if (OperatingSystem.IsAndroidVersionAtLeast(29))
                    StartForeground(NotificationId, n, ForegroundService.TypeConnectedDevice);
                else
                    StartForeground(NotificationId, n);
            }
            return StartCommandResult.Sticky;
        }
    }
}
