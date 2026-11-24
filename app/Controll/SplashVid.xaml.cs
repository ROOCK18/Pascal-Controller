using CommunityToolkit.Maui.Views;

namespace Controll;

public partial class SplashVid : ContentPage
{
    public SplashVid()
    {
        InitializeComponent();
    }

    // Ezt az eseményt a SplashVid.xaml-ben a MediaElementre kötjük rá:
    // MediaEnded="SplashVideo_MediaEnded"
    private void SplashVideo_MediaEnded(object sender, EventArgs e)
    {
        // Amint a videó lejátszódott, nyissa meg az AppShell-t
        Application.Current.Windows[0].Page = new AppShell();
    }
}
