namespace Controll.Services;

public static class UiHelper
{
    // Az aktuális (első) ablak gyökéroldala. Az Application.MainPage getter elavult.
    public static Page CurrentPage => Application.Current?.Windows.FirstOrDefault()?.Page;
}
