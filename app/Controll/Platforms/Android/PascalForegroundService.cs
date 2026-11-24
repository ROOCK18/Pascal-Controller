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
        private const string ChannelId = "pascal_background";
        private const int NotificationId = 1001;

        public override IBinder? OnBind(Intent? intent) => null;

        public override StartCommandResult OnStartCommand(Intent? intent, StartCommandFlags flags, int startId)
        {
            var nm = GetSystemService(NotificationService) as NotificationManager;
            if (nm != null && nm.GetNotificationChannel(ChannelId) == null)
            {
                var ch = new NotificationChannel(ChannelId, "Háttérfigyelés", NotificationImportance.Low)
                {
                    Description = "A Pascal-Controller kapcsolat és a közeledés-érzékelés fut a háttérben"
                };
                nm.CreateNotificationChannel(ch);
            }

            PendingIntent? pi = null;
            var launch = PackageManager?.GetLaunchIntentForPackage(PackageName ?? "");
            if (launch != null)
                pi = PendingIntent.GetActivity(this, 0, launch, PendingIntentFlags.Immutable | PendingIntentFlags.UpdateCurrent);

            var b = new NotificationCompat.Builder(this, ChannelId);
            b.SetContentTitle("Pascal-Controller");
            b.SetContentText("Háttérfigyelés aktív (közeledés / riasztások)");
            b.SetSmallIcon(ApplicationInfo?.Icon ?? 0);
            b.SetOngoing(true);
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
