namespace Controll;

public static class ServiceHelper
{
    public static T GetService<T>() where T : class
        => Current.GetService(typeof(T)) as T
           ?? throw new InvalidOperationException($"Nincs regisztrált szolgáltatás: {typeof(T).Name}");

    public static IServiceProvider Current => IPlatformApplication.Current?.Services
        ?? throw new InvalidOperationException("A szolgáltatások még nem érhetők el.");
}
