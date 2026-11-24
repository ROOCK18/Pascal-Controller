using Android.App;
using Android.Content.PM;
using Android.OS;
using Android.Widget;
using AndroidX.Activity;

namespace Controll
{
    [Activity(
        Theme = "@style/Maui.SplashTheme",
        MainLauncher = true,
        LaunchMode = LaunchMode.SingleTop,
        ConfigurationChanges = ConfigChanges.ScreenSize |
                               ConfigChanges.Orientation |
                               ConfigChanges.UiMode |
                               ConfigChanges.ScreenLayout |
                               ConfigChanges.SmallestScreenSize |
                               ConfigChanges.Density)]
    public class MainActivity : MauiAppCompatActivity
    {
        private DateTime _lastBackPressTime = DateTime.MinValue;

        protected override void OnCreate(Bundle? savedInstanceState)
        {
            base.OnCreate(savedInstanceState);

            OnBackPressedDispatcher.AddCallback(this, new BackPressedCallback(this));
        }

        private class BackPressedCallback : OnBackPressedCallback
        {
            private readonly MainActivity _activity;

            public BackPressedCallback(MainActivity activity) : base(true)
            {
                _activity = activity;
            }

            public override async void HandleOnBackPressed()
            {
                var shell = Microsoft.Maui.Controls.Application.Current?.Windows.FirstOrDefault()?.Page as AppShell;

                if (shell == null)
                {
                    _activity.Finish();
                    return;
                }

                // Ha van visszalépési lehetőség → lépjünk vissza
                if (Shell.Current.Navigation.NavigationStack.Count > 1)
                {
                    await Shell.Current.Navigation.PopAsync();
                    return;
                }

                // Itt már a főoldalon vagyunk → dupla kilépés
                var diff = DateTime.Now - _activity._lastBackPressTime;

                if (diff.TotalSeconds < 2)
                {
                    _activity.Finish();
                }
                else
                {
                    _activity._lastBackPressTime = DateTime.Now;
                    Toast.MakeText(_activity, "Nyomd meg újra a kilépéshez!", ToastLength.Short)?.Show();
                }
            }
        }

    }
}
