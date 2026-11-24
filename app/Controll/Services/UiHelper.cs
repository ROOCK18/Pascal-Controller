namespace Controll.Services;

public static class UiHelper
{
    // Az aktuális (első) ablak gyökéroldala. Az Application.MainPage getter elavult.
    // Színerőforrás az app erőforrás-szótárából (hiányzó kulcs esetén érthető kivétel).
    public static Color ThemeColor(string key)
        => Application.Current?.Resources.TryGetValue(key, out var v) == true && v is Color c
            ? c
            : throw new InvalidOperationException($"Hiányzó színerőforrás: {key}");

    public static Page CurrentPage => Application.Current?.Windows.FirstOrDefault()?.Page
        ?? throw new InvalidOperationException("Nincs aktív ablak.");
}
