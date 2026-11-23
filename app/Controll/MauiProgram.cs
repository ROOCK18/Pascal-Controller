using CommunityToolkit.Maui;
using Microsoft.Extensions.Logging;
using Controll.Services;
#if ANDROID
using Microsoft.Maui.Handlers;
#endif

namespace Controll;

public static class MauiProgram
{
    public static MauiApp CreateMauiApp()
    {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
            .UseMauiCommunityToolkitMediaElement(isAndroidForegroundServiceEnabled: false)
            .ConfigureFonts(fonts =>
            {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
                fonts.AddFont("OpenSans-Semibold.ttf", "OpenSansSemibold");
            });

        builder.Services.AddSingleton<IBluetoothService, BluetoothService>();

#if ANDROID
        // A natív Android Picker (MauiPicker, ami valójában egy szövegmező-
        // alapú vezérlő, nem Spinner) alapból a rendszer "indigo" témaszínét
        // használja az alávonáshoz - ez ütött el az app saját design-
        // nyelvétől. Itt felülírjuk a natív háttér-tint-et szürkére, hogy
        // illeszkedjen a többi vezérlőhöz (gombok, csúszkák stb.).
        PickerHandler.Mapper.AppendToMapping("CustomPickerTint", (handler, view) =>
        {
            try
            {
                var bg = handler.PlatformView.Background;
                if (bg != null)
                {
                    bg.Mutate();
                    bg.SetTint(global::Android.Graphics.Color.ParseColor("#3A3A3C"));
                }
            }
            catch { /* ha valamelyik régebbi Android verzión nincs ilyen API, szó nélkül hagyjuk az alapértelmezettet */ }
        });

        // Az Entry (beviteli mező) HÁTTERÉHEZ ne nyúljunk itt - azt a XAML
        // Style adja (LightGray), ugyanaz, mint a Picker-eké. Itt KIZÁRÓLAG
        // a szövegkurzor és a kijelölés-highlight színét állítjuk pirosra
        // (a natív lila helyett) - korábban tévedésből a teljes háttér-
        // drawable-t tinteltük, ami az egész mezőt pirosra festette.
        EntryHandler.Mapper.AppendToMapping("CustomEntryCursor", (handler, view) =>
        {
            try
            {
                var editText = handler.PlatformView;
                var color = global::Android.Graphics.Color.ParseColor("#FF453A");

                // Kijelölés-highlight (minden Android verzión működik)
                editText.SetHighlightColor(color);

                // Kurzor szín (csak Android 10 / API 29 és újabb támogatja
                // közvetlenül - régebbi verziókon szó nélkül kihagyjuk)
                if (global::Android.OS.Build.VERSION.SdkInt >= global::Android.OS.BuildVersionCodes.Q)
                {
                    var cursorDrawable = editText.TextCursorDrawable;
                    if (cursorDrawable != null)
                    {
                        cursorDrawable.Mutate();
                        cursorDrawable.SetTint(color);
                        editText.TextCursorDrawable = cursorDrawable;
                    }
                }
            }
            catch { /* ha valamelyik Android verzión nincs ilyen API, szó nélkül hagyjuk az alapértelmezettet */ }
        });
#endif

#if DEBUG
        builder.Logging.AddDebug();
#endif

        return builder.Build();
    }
}