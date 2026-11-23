using Controll.Pages;

namespace Controll;

public partial class AppShell : Shell
{
    public AppShell()
    {
        InitializeComponent();

        // Regisztráljuk az útvonalakat az inicializáláskor
        RegisterRoutes();
    }

    private void RegisterRoutes()
    {
        Routing.RegisterRoute("main", typeof(MainPage));
        Routing.RegisterRoute("settings", typeof(Settings));
        Routing.RegisterRoute("timing", typeof(Timing));
        Routing.RegisterRoute("rgb", typeof(RGB));
        Routing.RegisterRoute("data", typeof(Data));
        Routing.RegisterRoute("controll", typeof(Ctrl));
        Routing.RegisterRoute("note", typeof(Note));
    }

}