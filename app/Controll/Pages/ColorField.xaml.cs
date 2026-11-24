using System.Globalization;

namespace Controll.Pages;

// Színválasztó mező az RGB automatikákhoz: hex kód (RRGGBB) + előnézet + gyorsszínek.
public partial class ColorField : ContentView
{
    private static readonly string[] Presets =
    {
        "FFFFFF", "FFB46B", "FF0000", "FF8000", "FFFF00", "00FF00", "00FFFF", "0000FF", "8000FF", "FF00FF"
    };

    private bool _setting;

    public ColorField()
    {
        InitializeComponent();
        foreach (var hex in Presets)
        {
            var color = Color.FromArgb("#" + hex);
            var dot = new Border
            {
                WidthRequest = 28, HeightRequest = 28, Margin = new Thickness(0, 0, 8, 6),
                StrokeShape = new Microsoft.Maui.Controls.Shapes.RoundRectangle { CornerRadius = new CornerRadius(14) },
                Stroke = Colors.Black, StrokeThickness = 1, BackgroundColor = color
            };
            string h = hex;
            dot.GestureRecognizers.Add(new TapGestureRecognizer { Command = new Command(() => Hex = h) });
            Swatches.Add(dot);
        }
    }

    // A kiválasztott szín hex kódja, kettőskereszt nélkül (pl. "FF8000")
    public string Hex
    {
        get
        {
            var t = (HexEntry.Text ?? "").Trim().TrimStart('#').ToUpperInvariant();
            return t.Length == 6 && int.TryParse(t, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out _) ? t : "FFFFFF";
        }
        set
        {
            _setting = true;
            HexEntry.Text = (value ?? "FFFFFF").TrimStart('#').ToUpperInvariant();
            _setting = false;
            UpdatePreview();
        }
    }

    private void UpdatePreview()
    {
        try { Preview.Color = Color.FromArgb("#" + Hex); } catch { Preview.Color = Colors.White; }
    }

    private void OnHexChanged(object? sender, TextChangedEventArgs e)
    {
        if (!_setting) UpdatePreview();
    }
}
