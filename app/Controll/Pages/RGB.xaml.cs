using Controll.Services;
using Microsoft.Maui.Controls;
using Microsoft.Maui.Graphics;
using System.Diagnostics;

namespace Controll.Pages;

public partial class RGB : ContentView
{
    private bool _isUpdating = false;
    private long _lastSendTimestamp = 0;
    private const int MinSendIntervalMs = 45; // 45ms korlát a folyamatos küldéshez
    private CancellationTokenSource? _sendCts;

    public static readonly BindableProperty MixedColorProperty =
        BindableProperty.Create(nameof(MixedColor), typeof(Color), typeof(RGB), Colors.White);

    public Color MixedColor
    {
        get => (Color)GetValue(MixedColorProperty);
        set => SetValue(MixedColorProperty, value);
    }

    private double _hue = 0.0;
    private double _sat = 0.0;
    private double _val = 1.0;

    private double _centerX;
    private double _centerY;
    private double _radius;

    private readonly HsvWheelDrawable _drawable = new();
    private bool _colorEnabled = true;

    public RGB()
    {
        InitializeComponent();
        ColorPreviewBox.Color = MixedColor;
        HsvWheelView.Drawable = _drawable;

        HsvWheelView.SizeChanged += OnWheelSizeChanged;
        HsvWheelView.StartInteraction += OnWheelInteraction;
        HsvWheelView.DragInteraction += OnWheelInteraction;
        HsvWheelView.EndInteraction += OnWheelInteraction;
    }

    private void OnWheelSizeChanged(object? sender, EventArgs e)
    {
        if (HsvWheelView.Width <= 0 || HsvWheelView.Height <= 0) return;
        _centerX = HsvWheelView.Width / 2.0;
        _centerY = HsvWheelView.Height / 2.0;
        _radius = Math.Min(HsvWheelView.Width, HsvWheelView.Height) / 2.0 - 9.0;
        UpdateSelectorDotPosition();
    }

    private void OnWheelInteraction(object? sender, TouchEventArgs e)
    {
        if (e.Touches.Any())
        {
            var t = e.Touches[0];
            UpdateFromLocalPoint(new Point(t.X, t.Y));
        }
    }

    private void UpdateFromLocalPoint(Point p)
    {
        if (_radius <= 0) return;

        double dx = p.X - _centerX;
        double dy = p.Y - _centerY;
        double dist = Math.Sqrt(dx * dx + dy * dy);

        if (dist > _radius)
        {
            double scale = _radius / dist;
            dx *= scale; dy *= scale;
            dist = _radius;
        }

        double angle = Math.Atan2(dy, dx);
        if (angle < 0) angle += 2 * Math.PI;

        _hue = angle / (2 * Math.PI);
        _sat = dist / _radius;

        UpdateSelectorDotPosition();
        UpdateMixedColor();
    }

    private void UpdateSelectorDotPosition()
    {
        if (_radius <= 0) return;
        double angle = _hue * 2 * Math.PI;
        double dist = _sat * _radius;
        SelectorDot.TranslationX = Math.Cos(angle) * dist;
        SelectorDot.TranslationY = Math.Sin(angle) * dist;
    }

    private void OnValueChanged(object sender, ValueChangedEventArgs e)
    {
        if (_isUpdating) return;
        _val = e.NewValue;
        UpdateMixedColor();
    }

    private void UpdateMixedColor()
    {
        MixedColor = HsvToColor(_hue, _sat, _val);
        ColorPreviewBox.Color = MixedColor;

        int r = (int)(MixedColor.Red * 255);
        int g = (int)(MixedColor.Green * 255);
        int b = (int)(MixedColor.Blue * 255);
        HexLabel.Text = $"#{r:X2}{g:X2}{b:X2}";

        ScheduleSendToEsp();
    }

    // ========== OKOS KÜLDÉS (Throttling) ==========
    private async void ScheduleSendToEsp()
    {
        if (_isUpdating || !BluetoothManager.IsConnected) return;

        long currentTimestamp = DateTimeOffset.Now.ToUnixTimeMilliseconds();

        if (currentTimestamp - _lastSendTimestamp < MinSendIntervalMs)
        {
            _sendCts?.Cancel();
            _sendCts = new CancellationTokenSource();
            var token = _sendCts.Token;
            try
            {
                await Task.Delay(MinSendIntervalMs, token);
                if (!token.IsCancellationRequested) await PerformWrite();
            }
            catch (OperationCanceledException) { }
            return;
        }
        await PerformWrite();
    }

    private async Task PerformWrite()
    {
        _lastSendTimestamp = DateTimeOffset.Now.ToUnixTimeMilliseconds();
        int r = (int)(MixedColor.Red * 255);
        int g = (int)(MixedColor.Green * 255);
        int b = (int)(MixedColor.Blue * 255);
        await BluetoothManager.WriteAsync($"#{r:X2}{g:X2}{b:X2}");
    }

    // ========== HEX BEVITEL ÉS SZINKRON JAVÍTÁSA ==========
    private async void OnColorHexTapped(object sender, TappedEventArgs e)
    {
        Page currentPage = Shell.Current.CurrentPage;
        if (currentPage == null) return;

        string result = await currentPage.DisplayPromptAsync("Egyedi szín", "Kód:", "OK", "Mégse", HexLabel.Text, 7, Keyboard.Text);
        if (string.IsNullOrWhiteSpace(result)) return;

        try
        {
            Color newColor = Color.FromArgb(result.Trim());
            UpdateFromColorObject(newColor);
            ScheduleSendToEsp();
        }
        catch { await Shell.Current.DisplayAlertAsync("Hiba", "Érvénytelen HEX!", "OK"); }
    }

    private void UpdateFromColorObject(Color color)
    {
        _isUpdating = true;

        // Kézi konverzió ToHsv helyett
        double r = color.Red;
        double g = color.Green;
        double b = color.Blue;
        double max = Math.Max(r, Math.Max(g, b));
        double min = Math.Min(r, Math.Min(g, b));
        double delta = max - min;

        // Hue számítás
        if (delta == 0) _hue = 0;
        else if (max == r) _hue = ((g - b) / delta) % 6;
        else if (max == g) _hue = (b - r) / delta + 2;
        else _hue = (r - g) / delta + 4;
        _hue /= 6.0;
        if (_hue < 0) _hue += 1.0;

        // Saturation és Value
        _sat = (max == 0) ? 0 : delta / max;
        _val = max;

        // UI frissítés
        MixedColor = color;
        ColorPreviewBox.Color = color;
        ValueSlider.Value = _val;
        UpdateSelectorDotPosition();

        int red = (int)(r * 255);
        int green = (int)(g * 255);
        int blue = (int)(b * 255);
        HexLabel.Text = $"#{red:X2}{green:X2}{blue:X2}";

        _isUpdating = false;
    }

    // RGB automatikák oldal megnyitása
    private void OnRgbAutoClicked(object sender, TappedEventArgs e)
    {
        if (Shell.Current.CurrentPage is MainPage main) main.ShowRgbAuto();
    }

    // Color calibration page (camera based)
    private async void OnCalibrateClicked(object sender, TappedEventArgs e)
    {
        await Controll.Services.UiHelper.CurrentPage.Navigation.PushAsync(new RgbCalibrationPage());
    }

    private async void OnToggleTapped(object sender, TappedEventArgs e)
    {
        _colorEnabled = !_colorEnabled;
        UpdateToggleVisuals();
        await BluetoothManager.WriteAsync(_colorEnabled ? "RGB_1" : "RGB_0");
        if (_colorEnabled) ScheduleSendToEsp();
    }

    private void UpdateToggleVisuals()
    {
        ToggleBorder.BackgroundColor = _colorEnabled ? UiHelper.ThemeColor("PrimaryRed") : UiHelper.ThemeColor("LightGray");
        ToggleLabel.Text = _colorEnabled ? "ON" : "OFF";
    }

    private static Color HsvToColor(double h, double s, double v)
    {
        double hh = h * 6.0;
        int i = (int)Math.Floor(hh) % 6;
        double f = hh - Math.Floor(hh);
        double p = v * (1.0 - s);
        double q = v * (1.0 - f * s);
        double t = v * (1.0 - (1.0 - f) * s);
        (double r, double g, double b) = i switch
        {
            0 => (v, t, p),
            1 => (q, v, p),
            2 => (p, v, t),
            3 => (p, q, v),
            4 => (t, p, v),
            _ => (v, p, q)
        };
        return Color.FromRgb(r, g, b);
    }

    public async void InitializePage()
    {
        if (BluetoothManager.IsConnected)
        {
            BluetoothManager.OnDataReceived -= OnRgbDataReceived;
            BluetoothManager.OnDataReceived += OnRgbDataReceived;
            await BluetoothManager.WriteAsync("GET_RGB\n");
        }
    }

    private void OnRgbDataReceived(string data)
    {
        if (string.IsNullOrEmpty(data) || !data.Contains("RGB|")) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            try
            {
                string cleanData = data.Substring(data.IndexOf("RGB|")).Trim();
                string[] parts = cleanData.Split('|');
                if (parts.Length >= 3)
                {
                    Color color = Color.FromArgb(parts[1]);
                    UpdateFromColorObject(color);
                    _colorEnabled = parts[2] == "1";
                    UpdateToggleVisuals();
                }
            }
            catch { }
        });
    }
}

// =========================
// HSV Kerék Rajzoló
// =========================
public sealed class HsvWheelDrawable : IDrawable
{
    private const int Segments = 180;
    public void Draw(ICanvas canvas, RectF dirtyRect)
    {
        float cx = dirtyRect.Center.X;
        float cy = dirtyRect.Center.Y;
        float r = Math.Min(dirtyRect.Width, dirtyRect.Height) / 2f;

        for (int i = 0; i < Segments; i++)
        {
            float a1 = (float)(2 * Math.PI * i / Segments);
            float a2 = (float)(2 * Math.PI * (i + 1) / Segments);
            var col = HsvToColor((float)i / Segments, 1f, 1f);

            var path = new PathF();
            path.MoveTo(cx, cy);
            path.LineTo(cx + r * MathF.Cos(a1), cy + r * MathF.Sin(a1));
            path.LineTo(cx + r * MathF.Cos(a2), cy + r * MathF.Sin(a2));
            path.Close();

            canvas.FillColor = col;
            canvas.FillPath(path);
        }

        var radial = new RadialGradientPaint
        {
            Center = new Point(cx, cy),
            Radius = r,
            GradientStops = new PaintGradientStop[] {
                new PaintGradientStop(0.0f, Colors.White),
                new PaintGradientStop(1.0f, Colors.White.WithAlpha(0))
            }
        };
        canvas.SetFillPaint(radial, new RectF(cx - r, cy - r, 2 * r, 2 * r));
        canvas.FillCircle(cx, cy, r);
    }

    private static Color HsvToColor(float h, float s, float v)
    {
        float hh = h * 6f;
        int i = (int)MathF.Floor(hh) % 6;
        float f = hh - MathF.Floor(hh);
        float p = v * (1f - s);
        float q = v * (1f - f * s);
        float t = v * (1f - (1f - f) * s);
        return i switch
        {
            0 => Color.FromRgb(v, t, p),
            1 => Color.FromRgb(q, v, p),
            2 => Color.FromRgb(p, v, t),
            3 => Color.FromRgb(p, q, v),
            4 => Color.FromRgb(t, p, v),
            _ => Color.FromRgb(v, p, q)
        };
    }
}