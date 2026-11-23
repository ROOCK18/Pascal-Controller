// Pages/ThermoDrawables.cs
// Grafikus megjelenítők a Timing oldal termosztát panelekhez:
// - FanCurveDrawable: hőmérséklet -> fordulatszám % görbe (1-5 pont)
// - RelayThermoDrawable: BE/KI küszöb szemléltetése (fűtés/hűtés irány automatikus felismeréssel)

using Microsoft.Maui.Graphics;
using System;
using System.Collections.Generic;
using System.Linq;

namespace Controll.Pages;

public class FanCurveDrawable : IDrawable
{
    public List<(float T, int Pct, int Freq)> Points = new();
    public float CurrentTemp = float.NaN;
    public int CurrentPct = -1;
    public string Unit = "°C"; // "°C" vagy "%" (páratartalom forrás esetén)

    public void Draw(ICanvas canvas, RectF rect)
    {
        canvas.FillColor = Color.FromArgb("#1A1A1A");
        canvas.FillRoundedRectangle(rect, 8);

        if (Points.Count == 0) return;

        var pts = Points.OrderBy(p => p.T).ToList();
        float minT = pts.Min(p => p.T);
        float maxT = pts.Max(p => p.T);
        if (maxT <= minT) maxT = minT + 1;

        // A frekvencia tengely a TÉNYLEGES adat-tartományhoz igazodik (nem a
        // 10-25000 Hz teljes lehetséges skálához), kis margóval - így a
        // narancssárga vonal sosem "lapul le" a piros % vonal mellett.
        int minF = pts.Min(p => p.Freq), maxF = pts.Max(p => p.Freq);
        if (maxF <= minF) { minF = Math.Max(0, minF - 10); maxF += 10; }
        int margin = Math.Max(1, (maxF - minF) / 8);
        minF = Math.Max(0, minF - margin);
        maxF += margin;

        const float padL = 34, padR = 38, padT = 12, padB = 22;
        float w = rect.Width - padL - padR;
        float h = rect.Height - padT - padB;

        float X(float t) => rect.X + padL + (t - minT) / (maxT - minT) * w;
        float YPct(int pct) => rect.Y + padT + h - (pct / 100f) * h;
        float YFreq(int f) => rect.Y + padT + h - ((float)(f - minF) / (maxF - minF)) * h;

        // Rács + % feliratok (bal oldal)
        canvas.StrokeColor = Color.FromArgb("#333333");
        canvas.StrokeSize = 1;
        canvas.FontSize = 9;
        canvas.FontColor = Color.FromArgb("#777777");
        for (int p = 0; p <= 100; p += 25)
        {
            float y = YPct(p);
            canvas.DrawLine(rect.X + padL, y, rect.X + rect.Width - padR, y);
            canvas.DrawString($"{p}", rect.X, y - 6, padL - 4, 12, HorizontalAlignment.Right, VerticalAlignment.Center);
        }

        // Frekvencia felirat (jobb oldal, min/max)
        canvas.FontColor = Color.FromArgb("#e67e22");
        canvas.DrawString($"{maxF}", rect.X + rect.Width - padR + 2, rect.Y + padT - 2, padR - 2, 12, HorizontalAlignment.Left, VerticalAlignment.Top);
        canvas.DrawString($"{minF}", rect.X + rect.Width - padR + 2, rect.Y + rect.Height - padB - 10, padR - 2, 12, HorizontalAlignment.Left, VerticalAlignment.Top);

        // X tengely felirat (min/max hőmérséklet)
        canvas.FontColor = Color.FromArgb("#777777");
        canvas.DrawString($"{minT:F0}{Unit}", rect.X + padL, rect.Y + rect.Height - padB + 4, 50, 14, HorizontalAlignment.Left, VerticalAlignment.Top);
        canvas.DrawString($"{maxT:F0}{Unit}", rect.X + rect.Width - padR - 50, rect.Y + rect.Height - padB + 4, 50, 14, HorizontalAlignment.Right, VerticalAlignment.Top);

        // Fordulatszám görbe (piros) - a bal oldali 0-100% skálán
        canvas.StrokeColor = Color.FromArgb("#e74c3c");
        canvas.StrokeSize = 3;
        for (int i = 0; i < pts.Count - 1; i++)
            canvas.DrawLine(X(pts[i].T), YPct(pts[i].Pct), X(pts[i + 1].T), YPct(pts[i + 1].Pct));
        canvas.FillColor = Colors.White;
        foreach (var p in pts)
            canvas.FillCircle(X(p.T), YPct(p.Pct), 4);

        // Frekvencia görbe (narancssárga) - a jobb oldali, adat-tartományhoz
        // igazodó skálán, hogy a két vonal ne torzítsa egymást
        canvas.StrokeColor = Color.FromArgb("#e67e22");
        canvas.StrokeSize = 3;
        for (int i = 0; i < pts.Count - 1; i++)
            canvas.DrawLine(X(pts[i].T), YFreq(pts[i].Freq), X(pts[i + 1].T), YFreq(pts[i + 1].Freq));
        canvas.FillColor = Color.FromArgb("#e67e22");
        foreach (var p in pts)
            canvas.FillCircle(X(p.T), YFreq(p.Freq), 4);

        // Aktuális hőmérséklet / számított % jelölő
        if (!float.IsNaN(CurrentTemp) && CurrentPct >= 0)
        {
            float cx = X(Math.Clamp(CurrentTemp, minT, maxT));
            float cy = YPct(CurrentPct);
            canvas.StrokeColor = Color.FromArgb("#3498db");
            canvas.StrokeSize = 1;
            canvas.StrokeDashPattern = new float[] { 4, 3 };
            canvas.DrawLine(cx, rect.Y + padT, cx, rect.Y + rect.Height - padB);
            canvas.StrokeDashPattern = null;
            canvas.FillColor = Color.FromArgb("#3498db");
            canvas.FillCircle(cx, cy, 5);
        }
    }
}

public class RelayThermoDrawable : IDrawable
{
    public float On, Off;
    public float CurrentTemp = float.NaN;
    public string Unit = "°C"; // "°C" vagy "%" (páratartalom forrás esetén)

    public void Draw(ICanvas canvas, RectF rect)
    {
        canvas.FillColor = Color.FromArgb("#1A1A1A");
        canvas.FillRoundedRectangle(rect, 8);

        bool heating = On < Off;
        float lo = Math.Min(On, Off), hi = Math.Max(On, Off);
        float span = Math.Max(hi - lo, 1f);
        float minT = lo - span * 0.3f, maxT = hi + span * 0.3f;

        const float pad = 20;
        float w = rect.Width - pad * 2;
        float y = rect.Y + rect.Height / 2;

        float X(float t) => rect.X + pad + (t - minT) / (maxT - minT) * w;

        // Alap tengely
        canvas.StrokeColor = Color.FromArgb("#555555");
        canvas.StrokeSize = 2;
        canvas.DrawLine(rect.X + pad, y, rect.X + rect.Width - pad, y);

        // Aktív (relé BE) tartomány kiemelése
        canvas.FillColor = Color.FromArgb("#2ecc71").WithAlpha(0.25f);
        if (heating)
            canvas.FillRectangle(rect.X + pad, y - 9, X(On) - (rect.X + pad), 18);
        else
            canvas.FillRectangle(X(On), y - 9, (rect.X + rect.Width - pad) - X(On), 18);

        // BE jelölő (zöld)
        canvas.StrokeColor = Color.FromArgb("#2ecc71");
        canvas.StrokeSize = 3;
        canvas.DrawLine(X(On), y - 16, X(On), y + 16);
        canvas.FontSize = 11;
        canvas.FontColor = Color.FromArgb("#2ecc71");
        canvas.DrawString($"BE {On:F0}{Unit}", X(On) - 30, y - 34, 60, 16, HorizontalAlignment.Center, VerticalAlignment.Center);

        // KI jelölő (piros)
        canvas.StrokeColor = Color.FromArgb("#e74c3c");
        canvas.StrokeSize = 3;
        canvas.DrawLine(X(Off), y - 16, X(Off), y + 16);
        canvas.FontColor = Color.FromArgb("#e74c3c");
        canvas.DrawString($"KI {Off:F0}{Unit}", X(Off) - 30, y + 18, 60, 16, HorizontalAlignment.Center, VerticalAlignment.Center);

        // Irány felirat
        canvas.FontSize = 10;
        canvas.FontColor = Color.FromArgb("#999999");
        canvas.DrawString(heating ? "Fűtés logika: BE ha hideg, KI ha meleg" : "Hűtés logika: BE ha meleg, KI ha hideg",
            rect.X, rect.Y + 2, rect.Width, 14, HorizontalAlignment.Center, VerticalAlignment.Top);

        // Aktuális hőmérséklet
        if (!float.IsNaN(CurrentTemp))
        {
            float cx = X(Math.Clamp(CurrentTemp, minT, maxT));
            canvas.FillColor = Color.FromArgb("#3498db");
            canvas.FillCircle(cx, y, 6);
        }
    }
}
