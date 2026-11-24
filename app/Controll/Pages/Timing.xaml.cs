using Controll.Services;
// Pages/Timing.xaml.cs
using Microsoft.Maui.Controls;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

namespace Controll.Pages;

// Egy időzítő slot adatai
public class TimerSlot
{
    public int Hour   { get; set; } = 8;
    public int Minute { get; set; } = 0;
    public int Value  { get; set; } = 50;   // Fan: 0-100%, Relay: 0=ki 1=be
    public bool IsRelay { get; set; } = false;
}

public partial class Timing : ContentView
{
    // ── Szenzor értékek (MainPage tölti) ──
    public float Ntc1 { get; set; } = float.NaN;
    public float Ntc2 { get; set; } = float.NaN;

    // ── Nap bitmaszkok ──
    private int _fan1Days   = 0b1111111; // H-V alapból mind
    private int _fan2Days   = 0b1111111;
    private int _relay1Days = 0b1111111;
    private int _relay2Days = 0b1111111;

    // ── Slot listák ──
    private readonly List<TimerSlot> _fan1Slots   = new();
    private readonly List<TimerSlot> _fan2Slots   = new();
    private readonly List<TimerSlot> _relay1Slots = new();
    private readonly List<TimerSlot> _relay2Slots = new();

    // Megfelelteti a nap border-eket
    private Border[] _fan1DayBorders;
    private Border[] _fan2DayBorders;
    private Border[] _relay1DayBorders;
    private Border[] _relay2DayBorders;

    // ── Grafikus megjelenítők ──
    private readonly FanCurveDrawable   _fan1CurveDraw   = new();
    private readonly FanCurveDrawable   _fan2CurveDraw   = new();
    private readonly RelayThermoDrawable _relay1ThermoDraw = new();
    private readonly RelayThermoDrawable _relay2ThermoDraw = new();

    // Aktuális szenzor-elrendezés mód (0=2hőm 1=1hőm 2=pára 3=1kap1hőm 4=2kap)
    private int _currentMode = 0;

    public Timing()
    {
        InitializeComponent();

        _fan1DayBorders   = new[] { Fan1DayH, Fan1DayK, Fan1DaySz, Fan1DayCs, Fan1DayP, Fan1DaySzo, Fan1DayV };
        _fan2DayBorders   = new[] { Fan2DayH, Fan2DayK, Fan2DaySz, Fan2DayCs, Fan2DayP, Fan2DaySzo, Fan2DayV };
        _relay1DayBorders = new[] { Relay1DayH, Relay1DayK, Relay1DaySz, Relay1DayCs, Relay1DayP, Relay1DaySzo, Relay1DayV };
        _relay2DayBorders = new[] { Relay2DayH, Relay2DayK, Relay2DaySz, Relay2DayCs, Relay2DayP, Relay2DaySzo, Relay2DayV };

        Fan1ThermoSensor.SelectedIndex  = 0;
        Fan2ThermoSensor.SelectedIndex  = 0;
        Relay1ThermoSensor.SelectedIndex = 0;
        Relay2ThermoSensor.SelectedIndex = 0;

        Fan1CurveGraph.Drawable = _fan1CurveDraw;
        Fan2CurveGraph.Drawable = _fan2CurveDraw;
        Relay1ThermoGraph.Drawable = _relay1ThermoDraw;
        Relay2ThermoGraph.Drawable = _relay2ThermoDraw;

        Fan1PointCount.SelectedIndex = 4; // alapból mind az 5 pont aktív
        Fan2PointCount.SelectedIndex = 4;


        // Alapból mind nap ki van jelölve
        RefreshDayUI(_fan1DayBorders, _fan1Days);
        RefreshDayUI(_fan2DayBorders, _fan2Days);
        RefreshDayUI(_relay1DayBorders, _relay1Days);
        RefreshDayUI(_relay2DayBorders, _relay2Days);

        // Feliratkozás – ha jön GET_ALL válasz
        BluetoothManager.OnDataReceived -= OnTimingDataReceived;
        BluetoothManager.OnDataReceived += OnTimingDataReceived;
        BluetoothManager.OnDataReceived -= OnProxDataReceived;
        BluetoothManager.OnDataReceived += OnProxDataReceived;
    }

    // ══════════════ KÖZELEDÉS (csatornánként, a csatorna szekcióján belül; az eszközön tárolva) ══════════════
    // Az eszköz csak az Automata állapotú csatornát kapcsolja közeledéskor.
    private static readonly string[] ProxKeys = { "F1", "F2", "R1", "R2" };

    private (Microsoft.Maui.Controls.Switch sw, VerticalStackLayout panel, Border save, Entry? val, Entry dur) ProxControls(string key) => key switch
    {
        "F1" => (ProxF1Switch, ProxF1Panel, ProxF1Save, ProxF1ValueEntry, ProxF1DurEntry),
        "F2" => (ProxF2Switch, ProxF2Panel, ProxF2Save, ProxF2ValueEntry, ProxF2DurEntry),
        "R1" => (ProxR1Switch, ProxR1Panel, ProxR1Save, null, ProxR1DurEntry),
        _ => (ProxR2Switch, ProxR2Panel, ProxR2Save, null, ProxR2DurEntry)
    };

    // A kapcsoló bekapcsolásakor tovább nyílik a szekció a közeledés beállításaival
    private void OnProxSwitchToggled(object? sender, ToggledEventArgs e)
    {
        if (sender is not VisualElement b || !ProxKeys.Contains(b.AutomationId)) return;
        var (_, panel, save, _, _) = ProxControls(b.AutomationId);
        panel.IsVisible = e.Value;
        save.IsVisible = true;   // a kikapcsolás mentéséhez is kell a gomb
    }

    // A Timing megnyitásakor hívja a MainPage (a GET_ALL után)
    public async void RequestProx()
    {
        await Task.Delay(800);
        if (BluetoothManager.IsConnected) await BluetoothManager.WriteAsync("GET_PROX");
    }

    // LOAD_PROX:<csatorna>:<be>:<érték>:<időtartam>
    private void OnProxDataReceived(string data)
    {
        if (string.IsNullOrEmpty(data) || !data.StartsWith("LOAD_PROX:")) return;
        var p = data.Substring(10).Trim().Split(':');
        if (p.Length < 4 || !ProxKeys.Contains(p[0])) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            var (sw, panel, save, val, dur) = ProxControls(p[0]);
            if (val != null) val.Text = p[2];
            dur.Text = p[3];
            sw.IsToggled = p[1] == "1";
            panel.IsVisible = sw.IsToggled;
            save.IsVisible = sw.IsToggled;   // betöltéskor csak bekapcsolt állapotban látszik a mentés
        });
    }

    private async void OnSaveProxClicked(object? sender, TappedEventArgs e)
    {
        if (!BluetoothManager.IsConnected) return;
        string key = e.Parameter as string ?? "";
        if (!ProxKeys.Contains(key)) return;
        var page = Shell.Current.CurrentPage;
        var (sw, _, _, val, dur) = ProxControls(key);
        int value = 100;
        if (val != null && (!int.TryParse((val.Text ?? "").Trim(), out value) || value < 0 || value > 100))
        {
            await page.DisplayAlertAsync("Hibás érték", "A fordulatszám 0 és 100 közötti szám legyen.", "OK");
            return;
        }
        if (!int.TryParse((dur.Text ?? "").Trim(), out int seconds) || seconds < 0 || seconds > 65535)
        {
            await page.DisplayAlertAsync("Hibás érték", "Az időtartam 0 és 65535 másodperc közötti szám legyen.", "OK");
            return;
        }
        await BluetoothManager.WriteAsync($"SET_PROX:{key}:{(sw.IsToggled ? 1 : 0)}:{value}:{seconds}");
        await page.DisplayAlertAsync("Siker", "Közeledés beállítás elmentve.", "OK");
    }

    // ══════════════ VISSZA GOMB ══════════════
    // ══════════════ LENYITHATÓ KÁRTYA-FEJLÉCEK ══════════════
    // Alapból minden szekció (Ventilátor/Relé/Idő alapú kapcsolás) csukva
    // van - így az oldal sokkal rövidebb, és csak azt nyitod le, amit
    // épp be akarsz állítani.
    private void ToggleSection(VerticalStackLayout panel, Label icon)
    {
        panel.IsVisible = !panel.IsVisible;
        icon.Text = panel.IsVisible ? "▲" : "▼";
    }
    private void OnFan1HeaderTapped(object s, EventArgs e) => ToggleSection(Fan1DetailsPanel, Fan1ExpandIcon);
    private void OnFan2HeaderTapped(object s, EventArgs e) => ToggleSection(Fan2DetailsPanel, Fan2ExpandIcon);
    private void OnRelay1HeaderTapped(object s, EventArgs e) => ToggleSection(Relay1DetailsPanel, Relay1ExpandIcon);
    private void OnRelay2HeaderTapped(object s, EventArgs e) => ToggleSection(Relay2DetailsPanel, Relay2ExpandIcon);
    private void OnRelay1HoldHeaderTapped(object s, EventArgs e) => ToggleSection(Relay1HoldDetailsPanel, Relay1HoldExpandIcon);
    private void OnRelay2HoldHeaderTapped(object s, EventArgs e) => ToggleSection(Relay2HoldDetailsPanel, Relay2HoldExpandIcon);

    private void OnBackToControlClicked(object sender, EventArgs e)
    {
        BluetoothManager.OnDataReceived -= OnTimingDataReceived;
        if (Shell.Current.CurrentPage is MainPage main)
            main.ShowControlFromTiming();
    }

    // ══════════════ TAB VÁLTÁS ══════════════
    private void SetTab(Border activeTab, Border inactiveTab,
                        VerticalStackLayout showPanel, VerticalStackLayout hidePanel)
    {
        var red  = UiHelper.ThemeColor("PrimaryRed");
        var gray = Color.FromArgb("#444444");
        activeTab.BackgroundColor   = red;
        inactiveTab.BackgroundColor = gray;
        showPanel.IsVisible = true;
        hidePanel.IsVisible = false;
    }

    private void OnFan1TimerTabTapped(object s, TappedEventArgs e)
        => SetTab(Fan1TimerTab, Fan1ThermoTab, Fan1TimerPanel, Fan1ThermoPanel);
    private void OnFan1ThermoTabTapped(object s, TappedEventArgs e)
        => SetTab(Fan1ThermoTab, Fan1TimerTab, Fan1ThermoPanel, Fan1TimerPanel);

    private void OnFan2TimerTabTapped(object s, TappedEventArgs e)
        => SetTab(Fan2TimerTab, Fan2ThermoTab, Fan2TimerPanel, Fan2ThermoPanel);
    private void OnFan2ThermoTabTapped(object s, TappedEventArgs e)
        => SetTab(Fan2ThermoTab, Fan2TimerTab, Fan2ThermoPanel, Fan2TimerPanel);

    private void OnRelay1TimerTabTapped(object s, TappedEventArgs e)
        => SetTab(Relay1TimerTab, Relay1ThermoTab, Relay1TimerPanel, Relay1ThermoPanel);
    private void OnRelay1ThermoTabTapped(object s, TappedEventArgs e)
        => SetTab(Relay1ThermoTab, Relay1TimerTab, Relay1ThermoPanel, Relay1TimerPanel);

    private void OnRelay2TimerTabTapped(object s, TappedEventArgs e)
        => SetTab(Relay2TimerTab, Relay2ThermoTab, Relay2TimerPanel, Relay2ThermoPanel);
    private void OnRelay2ThermoTabTapped(object s, TappedEventArgs e)
        => SetTab(Relay2ThermoTab, Relay2TimerTab, Relay2ThermoPanel, Relay2TimerPanel);

    // ══════════════ NAPOK ══════════════
    private void ToggleDay(Border[] borders, ref int mask, int bit)
    {
        mask ^= (1 << bit);
        RefreshDayUI(borders, mask);
    }

    private void RefreshDayUI(Border[] borders, int mask)
    {
        var red  = UiHelper.ThemeColor("PrimaryRed");
        var gray = Color.FromArgb("#444444");
        for (int i = 0; i < 7; i++)
            borders[i].BackgroundColor = (mask & (1 << i)) != 0 ? red : gray;
    }

    private void OnFan1DayTapped(object s, TappedEventArgs e)
    {
        if (int.TryParse(e.Parameter?.ToString(), out int bit))
            ToggleDay(_fan1DayBorders, ref _fan1Days, bit);
    }
    private void OnFan2DayTapped(object s, TappedEventArgs e)
    {
        if (int.TryParse(e.Parameter?.ToString(), out int bit))
            ToggleDay(_fan2DayBorders, ref _fan2Days, bit);
    }
    private void OnRelay1DayTapped(object s, TappedEventArgs e)
    {
        if (int.TryParse(e.Parameter?.ToString(), out int bit))
            ToggleDay(_relay1DayBorders, ref _relay1Days, bit);
    }
    private void OnRelay2DayTapped(object s, TappedEventArgs e)
    {
        if (int.TryParse(e.Parameter?.ToString(), out int bit))
            ToggleDay(_relay2DayBorders, ref _relay2Days, bit);
    }

    // ══════════════ SLOT UI ÉPÍTÉS ══════════════
    private View BuildFanSlotView(List<TimerSlot> slots, int idx)
    {
        var slot = slots[idx];

        var timePicker = new TimePicker
        {
            Time = new TimeSpan(slot.Hour, slot.Minute, 0),
            BackgroundColor = Color.FromArgb("#3A3A3A"),
            TextColor = Colors.White
        };
        timePicker.PropertyChanged += (s, e) =>
        {
            if (e.PropertyName == nameof(TimePicker.Time))
            {
                var t = timePicker.Time ?? TimeSpan.Zero;
                slot.Hour   = t.Hours;
                slot.Minute = t.Minutes;
            }
        };

        var valueLabel = new Label
        {
            Text = $"{slot.Value}%",
            TextColor = Colors.White,
            VerticalOptions = LayoutOptions.Center,
            WidthRequest = 38
        };

        var slider = new Slider
        {
            Minimum = 0, Maximum = 100, Value = slot.Value,
            ThumbColor = Colors.White,
            MinimumTrackColor = UiHelper.ThemeColor("PrimaryRed"),
            MaximumTrackColor = Color.FromArgb("#555555"),
            HorizontalOptions = LayoutOptions.Fill
        };
        slider.ValueChanged += (s, e) =>
        {
            slot.Value = (int)e.NewValue;
            valueLabel.Text = $"{slot.Value}%";
        };

        var deleteBtn = new Button
        {
            Text = "✕",
            BackgroundColor = Color.FromArgb("#8B0000"),
            TextColor = Colors.White,
            WidthRequest = 38, HeightRequest = 38,
            CornerRadius = 6, Padding = new Thickness(0)
        };
        deleteBtn.Clicked += (s, e) =>
        {
            slots.RemoveAt(idx);
            RebuildSlots(slots, idx < slots.Count ? idx : slots.Count);
        };

        return new Border
        {
            BackgroundColor = Color.FromArgb("#1A1A1A"),
            StrokeShape = new Microsoft.Maui.Controls.Shapes.RoundRectangle { CornerRadius = 8 },
            Padding = new Thickness(10, 8),
            Content = new Grid
            {
                ColumnDefinitions =
                {
                    new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) },
                    new ColumnDefinition { Width = new GridLength(2, GridUnitType.Star) },
                    new ColumnDefinition { Width = GridLength.Auto },
                    new ColumnDefinition { Width = GridLength.Auto }
                },
                ColumnSpacing = 8,
                Children =
                {
                    timePicker.AssignColumn(0),
                    slider.AssignColumn(1),
                    valueLabel.AssignColumn(2),
                    deleteBtn.AssignColumn(3)
                }
            }
        };
    }

    private View BuildRelaySlotView(List<TimerSlot> slots, int idx)
    {
        var slot = slots[idx];

        var timePicker = new TimePicker
        {
            Time = new TimeSpan(slot.Hour, slot.Minute, 0),
            BackgroundColor = Color.FromArgb("#3A3A3A"),
            TextColor = Colors.White
        };
        timePicker.PropertyChanged += (s, e) =>
        {
            if (e.PropertyName == nameof(TimePicker.Time))
            {
                var t = timePicker.Time ?? TimeSpan.Zero;
                slot.Hour   = t.Hours;
                slot.Minute = t.Minutes;
            }
        };

        var stateBtn = new Border
        {
            BackgroundColor = slot.Value == 1
                ? UiHelper.ThemeColor("PrimaryRed")
                : Color.FromArgb("#444444"),
            StrokeShape = new Microsoft.Maui.Controls.Shapes.RoundRectangle { CornerRadius = 6 },
            Padding = new Thickness(14, 8),
            VerticalOptions = LayoutOptions.Center
        };
        var stateLabel = new Label
        {
            Text = slot.Value == 1 ? "BE" : "KI",
            TextColor = Colors.White,
            FontAttributes = FontAttributes.Bold,
            FontSize = 13
        };
        stateBtn.Content = stateLabel;
        stateBtn.GestureRecognizers.Add(new TapGestureRecognizer
        {
            Command = new Command(() =>
            {
                slot.Value = slot.Value == 1 ? 0 : 1;
                stateLabel.Text = slot.Value == 1 ? "BE" : "KI";
                var red  = UiHelper.ThemeColor("PrimaryRed");
                stateBtn.BackgroundColor = slot.Value == 1 ? red : Color.FromArgb("#444444");
            })
        });

        var deleteBtn = new Button
        {
            Text = "✕",
            BackgroundColor = Color.FromArgb("#8B0000"),
            TextColor = Colors.White,
            WidthRequest = 38, HeightRequest = 38,
            CornerRadius = 6, Padding = new Thickness(0)
        };
        deleteBtn.Clicked += (s, e) =>
        {
            slots.RemoveAt(idx);
            RebuildRelaySlots(slots, idx < slots.Count ? idx : slots.Count);
        };

        return new Border
        {
            BackgroundColor = Color.FromArgb("#1A1A1A"),
            StrokeShape = new Microsoft.Maui.Controls.Shapes.RoundRectangle { CornerRadius = 8 },
            Padding = new Thickness(10, 8),
            Content = new Grid
            {
                ColumnDefinitions =
                {
                    new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) },
                    new ColumnDefinition { Width = GridLength.Auto },
                    new ColumnDefinition { Width = GridLength.Auto }
                },
                ColumnSpacing = 8,
                Children =
                {
                    timePicker.AssignColumn(0),
                    stateBtn.AssignColumn(1),
                    deleteBtn.AssignColumn(2)
                }
            }
        };
    }

    // Segédmetódus: visszaépíti a slot konténert
    private void RebuildSlots(List<TimerSlot> slots, int _)
    {
        // Meghatározzuk melyik konténerhez tartozik
        VerticalStackLayout? container = null;
        Border? addBtn = null;
        if (slots == _fan1Slots)   { container = Fan1SlotsContainer;   addBtn = Fan1AddSlotBtn; }
        if (slots == _fan2Slots)   { container = Fan2SlotsContainer;   addBtn = Fan2AddSlotBtn; }
        if (container == null || addBtn == null) return;   // ismeretlen szerkesztett lista: nincs mit újrarajzolni
        RedrawFanSlots(slots, container, addBtn);
    }

    private void RebuildRelaySlots(List<TimerSlot> slots, int _)
    {
        VerticalStackLayout? container = null;
        Border? addBtn = null;
        if (slots == _relay1Slots) { container = Relay1SlotsContainer; addBtn = Relay1AddSlotBtn; }
        if (slots == _relay2Slots) { container = Relay2SlotsContainer; addBtn = Relay2AddSlotBtn; }
        if (container == null || addBtn == null) return;
        RedrawRelaySlots(slots, container, addBtn);
    }

    private void RedrawFanSlots(List<TimerSlot> slots, VerticalStackLayout container, Border addBtn)
    {
        container.Children.Clear();
        for (int i = 0; i < slots.Count; i++)
            container.Children.Add(BuildFanSlotView(slots, i));
        if (addBtn != null) addBtn.IsEnabled = slots.Count < 5;
    }

    private void RedrawRelaySlots(List<TimerSlot> slots, VerticalStackLayout container, Border addBtn)
    {
        container.Children.Clear();
        for (int i = 0; i < slots.Count; i++)
            container.Children.Add(BuildRelaySlotView(slots, i));
        if (addBtn != null) addBtn.IsEnabled = slots.Count < 5;
    }

    // ══════════════ SLOT HOZZÁADÁS ══════════════
    private void OnFan1AddSlot(object s, EventArgs e)
    {
        if (_fan1Slots.Count >= 5) return;
        _fan1Slots.Add(new TimerSlot());
        RedrawFanSlots(_fan1Slots, Fan1SlotsContainer, Fan1AddSlotBtn);
    }
    private void OnFan2AddSlot(object s, EventArgs e)
    {
        if (_fan2Slots.Count >= 5) return;
        _fan2Slots.Add(new TimerSlot());
        RedrawFanSlots(_fan2Slots, Fan2SlotsContainer, Fan2AddSlotBtn);
    }
    private void OnRelay1AddSlot(object s, EventArgs e)
    {
        if (_relay1Slots.Count >= 5) return;
        _relay1Slots.Add(new TimerSlot { IsRelay = true, Value = 1 });
        RedrawRelaySlots(_relay1Slots, Relay1SlotsContainer, Relay1AddSlotBtn);
    }
    private void OnRelay2AddSlot(object s, EventArgs e)
    {
        if (_relay2Slots.Count >= 5) return;
        _relay2Slots.Add(new TimerSlot { IsRelay = true, Value = 1 });
        RedrawRelaySlots(_relay2Slots, Relay2SlotsContainer, Relay2AddSlotBtn);
    }

    // ══════════════ TERMOSZTÁT LIVE PREVIEW ══════════════
    private void OnFan1ThermoChanged(object s, EventArgs e) => UpdateFanPreview(
        Fan1ThermoSensor,
        new[] { Fan1P1T, Fan1P2T, Fan1P3T, Fan1P4T, Fan1P5T },
        new[] { Fan1P1Pct, Fan1P2Pct, Fan1P3Pct, Fan1P4Pct, Fan1P5Pct },
        new[] { Fan1P1Freq, Fan1P2Freq, Fan1P3Freq, Fan1P4Freq, Fan1P5Freq },
        Fan1ThermoPreview, Fan1PointCount, _fan1CurveDraw, Fan1CurveGraph);
    private void OnFan2ThermoChanged(object s, EventArgs e) => UpdateFanPreview(
        Fan2ThermoSensor,
        new[] { Fan2P1T, Fan2P2T, Fan2P3T, Fan2P4T, Fan2P5T },
        new[] { Fan2P1Pct, Fan2P2Pct, Fan2P3Pct, Fan2P4Pct, Fan2P5Pct },
        new[] { Fan2P1Freq, Fan2P2Freq, Fan2P3Freq, Fan2P4Freq, Fan2P5Freq },
        Fan2ThermoPreview, Fan2PointCount, _fan2CurveDraw, Fan2CurveGraph);

    // A Pontok száma Picker aktuális értéke (1-5, alapból 5 ha még nincs kiválasztva)
    private static int ActivePointCount(Picker p) => p.SelectedIndex < 0 ? 5 : p.SelectedIndex + 1;

    // Igaz, ha az adott "Érzékelő" választón a Páratartalom van kiválasztva
    // (csak "páratartalom" módban (mode 2) értelmezhető, ott a Picker 2. eleme).
    private bool IsHumiditySelected(Picker sensorPicker) => _currentMode == 2 && sensorPicker.SelectedIndex == 1;

    // Csak az aktív ("Pontok száma" által meghatározott) pontokat olvassa be,
    // és hőmérséklet szerint sorba rendezi.
    private (float T, int Pct, int Freq)[] GetFanCurve(Entry[] tEntries, Entry[] pEntries, Entry[] fEntries, int count)
    {
        count = Math.Clamp(count, 1, 5);
        var pts = new (float T, int Pct, int Freq)[count];
        for (int i = 0; i < count; i++)
        {
            F(tEntries[i].Text, out float t);
            int p = int.TryParse(pEntries[i].Text, out int pv) ? Math.Clamp(pv, 0, 100) : 0;
            int f = int.TryParse(fEntries[i].Text, out int fv) ? Math.Clamp(fv, 10, 25000) : 30;
            pts[i] = (t, p, f);
        }
        Array.Sort(pts, (a, b) => a.T.CompareTo(b.T));
        return pts;
    }

    // Lineáris interpoláció a görbe pontjai között (a szélein kilóg a szélső %-nál).
    // 1 pont esetén konstans értéket ad vissza.
    private int InterpolateFanCurve((float T, int Pct, int Freq)[] pts, float temp)
    {
        if (pts.Length == 1) return pts[0].Pct;
        if (temp <= pts[0].T) return pts[0].Pct;
        for (int i = 0; i < pts.Length - 1; i++)
        {
            if (temp <= pts[i + 1].T)
            {
                float span = pts[i + 1].T - pts[i].T;
                if (span <= 0) return pts[i].Pct;
                float frac = (temp - pts[i].T) / span;
                return (int)Math.Round(pts[i].Pct + frac * (pts[i + 1].Pct - pts[i].Pct));
            }
        }
        return pts[^1].Pct;
    }

    private void UpdateFanPreview(Picker sensor, Entry[] tEntries, Entry[] pEntries, Entry[] fEntries, Label preview,
        Picker pointCount, FanCurveDrawable draw, GraphicsView graph)
    {
        int count = ActivePointCount(pointCount);
        var pts = GetFanCurve(tEntries, pEntries, fEntries, count);
        draw.Points = pts.ToList();

        bool humidity = IsHumiditySelected(sensor);
        draw.Unit = humidity ? "%" : "°C";

        if (humidity)
        {
            // Ezen a hardveren nincs bekötve páratartalom-szenzor -> nincs élő adat,
            // ezért nem jelenítünk meg (téves) hőmérséklet-értéket %-ként.
            preview.Text = "-- %  →  -- %";
            draw.CurrentTemp = float.NaN;
            draw.CurrentPct = -1;
        }
        else
        {
            float temp = sensor.SelectedIndex == 1 ? Ntc2 : Ntc1;
            if (float.IsNaN(temp))
            {
                preview.Text = "-- °C  →  -- %";
                draw.CurrentTemp = float.NaN;
                draw.CurrentPct = -1;
            }
            else
            {
                int pct = InterpolateFanCurve(pts, temp);
                preview.Text = $"{temp:F1} °C  →  {pct} %";
                draw.CurrentTemp = temp;
                draw.CurrentPct = pct;
            }
        }
        graph.Invalidate();
    }

    // Pontok száma váltásakor a felesleges sorokat elrejtjük, hogy ne kelljen
    // mindig mind az 5 paramétert kitölteni.
    private void OnFan1PointCountChanged(object s, EventArgs e)
    {
        SetPointRowsVisible(ActivePointCount(Fan1PointCount),
            new View[] { Fan1P1T, Fan1P1Pct, Fan1P1Freq }, new View[] { Fan1P2T, Fan1P2Pct, Fan1P2Freq },
            new View[] { Fan1P3T, Fan1P3Pct, Fan1P3Freq }, new View[] { Fan1P4T, Fan1P4Pct, Fan1P4Freq },
            new View[] { Fan1P5T, Fan1P5Pct, Fan1P5Freq });
        OnFan1ThermoChanged(s, e);
    }
    private void OnFan2PointCountChanged(object s, EventArgs e)
    {
        SetPointRowsVisible(ActivePointCount(Fan2PointCount),
            new View[] { Fan2P1T, Fan2P1Pct, Fan2P1Freq }, new View[] { Fan2P2T, Fan2P2Pct, Fan2P2Freq },
            new View[] { Fan2P3T, Fan2P3Pct, Fan2P3Freq }, new View[] { Fan2P4T, Fan2P4Pct, Fan2P4Freq },
            new View[] { Fan2P5T, Fan2P5Pct, Fan2P5Freq });
        OnFan2ThermoChanged(s, e);
    }
    private void SetPointRowsVisible(int count, params View[][] rows)
    {
        for (int i = 0; i < rows.Length; i++)
        {
            bool visible = i < count;
            foreach (var v in rows[i]) v.IsVisible = visible;
        }
    }

    private void OnRelay1ThermoChanged(object s, EventArgs e)
    {
        bool humidity = IsHumiditySelected(Relay1ThermoSensor);
        string unit = humidity ? "%" : "°C";
        Relay1OnLabel.Text = $"Bekapcsol ({unit})";
        Relay1OffLabel.Text = $"Kikapcsol ({unit})";
        Relay1CurrentLabel.Text = humidity ? "Aktuális páratartalom" : "Aktuális hőmérséklet";

        if (F(Relay1TOn.Text, out float on) && F(Relay1TOff.Text, out float off))
        {
            _relay1ThermoDraw.On = on;
            _relay1ThermoDraw.Off = off;
            _relay1ThermoDraw.Unit = unit;
            // Nincs bekötött páratartalom-szenzor -> páratartalom módban nincs élő érték
            _relay1ThermoDraw.CurrentTemp = humidity ? float.NaN : (Relay1ThermoSensor.SelectedIndex == 1 ? Ntc2 : Ntc1);
            Relay1ThermoGraph.Invalidate();
        }
        Relay1CurrentTemp.Text = humidity ? "-- %" : (float.IsNaN(Ntc1) ? "-- °C" : $"{(Relay1ThermoSensor.SelectedIndex == 1 ? Ntc2 : Ntc1):F1} °C");
    }
    private void OnRelay2ThermoChanged(object s, EventArgs e)
    {
        bool humidity = IsHumiditySelected(Relay2ThermoSensor);
        string unit = humidity ? "%" : "°C";
        Relay2OnLabel.Text = $"Bekapcsol ({unit})";
        Relay2OffLabel.Text = $"Kikapcsol ({unit})";
        Relay2CurrentLabel.Text = humidity ? "Aktuális páratartalom" : "Aktuális hőmérséklet";

        if (F(Relay2TOn.Text, out float on) && F(Relay2TOff.Text, out float off))
        {
            _relay2ThermoDraw.On = on;
            _relay2ThermoDraw.Off = off;
            _relay2ThermoDraw.Unit = unit;
            _relay2ThermoDraw.CurrentTemp = humidity ? float.NaN : (Relay2ThermoSensor.SelectedIndex == 1 ? Ntc2 : Ntc1);
            Relay2ThermoGraph.Invalidate();
        }
        Relay2CurrentTemp.Text = humidity ? "-- %" : (float.IsNaN(Ntc2) ? "-- °C" : $"{(Relay2ThermoSensor.SelectedIndex == 1 ? Ntc2 : Ntc1):F1} °C");
    }

    // ══════════════ BLE MENTÉS ══════════════

    // Fan timer: SET_FTIMER:F1:<days>:<H>:<M>:<pct>|<H>:<M>:<pct>|...
    private async void OnFan1TimerSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan1TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        string cmd = BuildFanTimerCmd("F1", _fan1Days, _fan1Slots);
        await BluetoothManager.WriteAsync(cmd);
        Fan1TimerStatus.Text = $"Elküldve ({_fan1Slots.Count} slot)";
    }
    private async void OnFan2TimerSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan2TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        string cmd = BuildFanTimerCmd("F2", _fan2Days, _fan2Slots);
        await BluetoothManager.WriteAsync(cmd);
        Fan2TimerStatus.Text = $"Elküldve ({_fan2Slots.Count} slot)";
    }

    // Relay timer: SET_RTIMER:R1:<days>:<H>:<M>:<0/1>|...
    private async void OnRelay1TimerSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        string cmd = BuildRelayTimerCmd("R1", _relay1Days, _relay1Slots);
        await BluetoothManager.WriteAsync(cmd);
        Relay1TimerStatus.Text = $"Elküldve ({_relay1Slots.Count} slot)";
    }
    private async void OnRelay2TimerSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        string cmd = BuildRelayTimerCmd("R2", _relay2Days, _relay2Slots);
        await BluetoothManager.WriteAsync(cmd);
        Relay2TimerStatus.Text = $"Elküldve ({_relay2Slots.Count} slot)";
    }

    // Fan thermo: SET_THERM:F1:<en>:<sensor>:<count>:<t1>:<p1>:...:<tn>:<pn>[:<freqHz>]
    private async void OnFan1ThermoSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan1ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        int count = ActivePointCount(Fan1PointCount);
        var pts = GetFanCurve(
            new[] { Fan1P1T, Fan1P2T, Fan1P3T, Fan1P4T, Fan1P5T },
            new[] { Fan1P1Pct, Fan1P2Pct, Fan1P3Pct, Fan1P4Pct, Fan1P5Pct },
            new[] { Fan1P1Freq, Fan1P2Freq, Fan1P3Freq, Fan1P4Freq, Fan1P5Freq }, count);
        int sensor = SensorByte(Fan1ThermoSensor, _currentMode == 2);
        string cmd = $"SET_THERM:F1:1:{sensor}:{count}:" +
            string.Join(":", pts.Select(p => $"{p.T:F1}:{p.Pct}:{p.Freq}"));
        await BluetoothManager.WriteAsync(cmd);
        Fan1ThermoStatus.Text = "Görbe elküldve";
    }
    private async void OnFan1ThermoDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan1ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_THERM:F1");
        Fan1ThermoStatus.Text = "Törölve";
    }
    private async void OnFan2ThermoSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan2ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        int count = ActivePointCount(Fan2PointCount);
        var pts = GetFanCurve(
            new[] { Fan2P1T, Fan2P2T, Fan2P3T, Fan2P4T, Fan2P5T },
            new[] { Fan2P1Pct, Fan2P2Pct, Fan2P3Pct, Fan2P4Pct, Fan2P5Pct },
            new[] { Fan2P1Freq, Fan2P2Freq, Fan2P3Freq, Fan2P4Freq, Fan2P5Freq }, count);
        int sensor = SensorByte(Fan2ThermoSensor, _currentMode == 2);
        string cmd = $"SET_THERM:F2:1:{sensor}:{count}:" +
            string.Join(":", pts.Select(p => $"{p.T:F1}:{p.Pct}:{p.Freq}"));
        await BluetoothManager.WriteAsync(cmd);
        Fan2ThermoStatus.Text = "Görbe elküldve";
    }
    private async void OnFan2ThermoDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan2ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_THERM:F2");
        Fan2ThermoStatus.Text = "Törölve";
    }
    private async void OnRelay1ThermoSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        if (!F(Relay1TOn.Text, out float on) || !F(Relay1TOff.Text, out float off))
        { Relay1ThermoStatus.Text = "Hibás értékek!"; return; }
        int sensor = SensorByte(Relay1ThermoSensor, _currentMode == 2);
        await BluetoothManager.WriteAsync($"SET_THERM:R1:1:{on:F1}:{off:F1}:{sensor}");
        Relay1ThermoStatus.Text = $"Elküldve: BE≥{on}°C KI≤{off}°C (NTC{sensor})";
    }
    private async void OnRelay1ThermoDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_THERM:R1");
        Relay1ThermoStatus.Text = "Törölve";
    }
    private async void OnRelay2ThermoSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        if (!F(Relay2TOn.Text, out float on) || !F(Relay2TOff.Text, out float off))
        { Relay2ThermoStatus.Text = "Hibás értékek!"; return; }
        int sensor = SensorByte(Relay2ThermoSensor, _currentMode == 2);
        await BluetoothManager.WriteAsync($"SET_THERM:R2:1:{on:F1}:{off:F1}:{sensor}");
        Relay2ThermoStatus.Text = $"Elküldve: BE≥{on}°C KI≤{off}°C (NTC{sensor})";
    }
    private async void OnRelay2ThermoDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2ThermoStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_THERM:R2");
        Relay2ThermoStatus.Text = "Törölve";
    }

    // ══════════════ MÓD-FÜGGŐ MEGJELENÍTÉS ══════════════
    // Mode 4 = "2 kapcsoló": nincs folytonos mérőérték, amihez küszöböt
    // lehetne szabni -> minden csatornánál (2 ventilátor + 2 relé) a
    // Termosztát tartalom helyett egy Kapcsoló-logika (K1/K2 -> érték)
    // szerkesztő jelenik meg, és a fül felirata is "⚡ Logika"-ra vált.
    // Mode 2 = "DHT11 hő+pára": az Érzékelő választó Páratartalom opciót
    // is felkínál a Hőmérséklet mellett.
    private void ApplyModeVisibility()
    {
        bool tempSwitchMode = _currentMode == 3; // "1 kapcsoló+1 hőmérő"
        bool twoSwitchMode  = _currentMode == 4; // "2 kapcsoló"

        Fan1ControlSourceRow.IsVisible = tempSwitchMode;
        Fan2ControlSourceRow.IsVisible = tempSwitchMode;
        Relay1ControlSourceRow.IsVisible = tempSwitchMode;
        Relay2ControlSourceRow.IsVisible = tempSwitchMode;

        if (tempSwitchMode)
        {
            if (Fan1ControlSource.SelectedIndex < 0) Fan1ControlSource.SelectedIndex = 0;
            if (Fan2ControlSource.SelectedIndex < 0) Fan2ControlSource.SelectedIndex = 0;
            if (Relay1ControlSource.SelectedIndex < 0) Relay1ControlSource.SelectedIndex = 0;
            if (Relay2ControlSource.SelectedIndex < 0) Relay2ControlSource.SelectedIndex = 0;
            if (Fan1LogicOOMode.SelectedIndex < 0) Fan1LogicOOMode.SelectedIndex = 0;
            if (Fan1LogicCOMode.SelectedIndex < 0) Fan1LogicCOMode.SelectedIndex = 0;
            if (Fan2LogicOOMode.SelectedIndex < 0) Fan2LogicOOMode.SelectedIndex = 0;
            if (Fan2LogicCOMode.SelectedIndex < 0) Fan2LogicCOMode.SelectedIndex = 0;
            ApplyFanControlSource(Fan1ControlSource, Fan1ThermoNormal, Fan1LogicSection, Fan1ThermoTabLabel,
                Fan1LogicIntro, Fan1LogicRow1, Fan1LogicRow2, Fan1LogicRow3, Fan1LogicRow4, Fan1LogicOC, Fan1LogicCC);
            ApplyFanControlSource(Fan2ControlSource, Fan2ThermoNormal, Fan2LogicSection, Fan2ThermoTabLabel,
                Fan2LogicIntro, Fan2LogicRow1, Fan2LogicRow2, Fan2LogicRow3, Fan2LogicRow4, Fan2LogicOC, Fan2LogicCC);
            ApplyRelayControlSource(Relay1ControlSource, Relay1ThermoNormal, Relay1LogicSection, Relay1ThermoTabLabel,
                Relay1LogicIntro, Relay1LogicRow1, Relay1LogicRow2, Relay1LogicRow3, Relay1LogicRow4, Relay1LogicOC, Relay1LogicCC);
            ApplyRelayControlSource(Relay2ControlSource, Relay2ThermoNormal, Relay2LogicSection, Relay2ThermoTabLabel,
                Relay2LogicIntro, Relay2LogicRow1, Relay2LogicRow2, Relay2LogicRow3, Relay2LogicRow4, Relay2LogicOC, Relay2LogicCC);
        }
        else
        {
            SetChannelMode(Fan1ThermoNormal, Fan1LogicSection, Fan1ThermoTabLabel, twoSwitchMode);
            SetChannelMode(Fan2ThermoNormal, Fan2LogicSection, Fan2ThermoTabLabel, twoSwitchMode);
            SetChannelMode(Relay1ThermoNormal, Relay1LogicSection, Relay1ThermoTabLabel, twoSwitchMode);
            SetChannelMode(Relay2ThermoNormal, Relay2LogicSection, Relay2ThermoTabLabel, twoSwitchMode);

            SetTwoSwitchRowLabels(Fan1LogicIntro, Fan1LogicRow1, Fan1LogicRow2, Fan1LogicRow3, Fan1LogicRow4, Fan1LogicOC, Fan1LogicCC, isFan: true);
            SetTwoSwitchRowLabels(Fan2LogicIntro, Fan2LogicRow1, Fan2LogicRow2, Fan2LogicRow3, Fan2LogicRow4, Fan2LogicOC, Fan2LogicCC, isFan: true);
            // 2 kapcsoló módban nincs "Görbe szerint" opció - mind a 4 sor egyszerű %.
            SetFanRowPlainPercent(Fan1LogicOOMode, Fan1LogicOO);
            SetFanRowPlainPercent(Fan1LogicCOMode, Fan1LogicCO);
            SetFanRowPlainPercent(Fan2LogicOOMode, Fan2LogicOO);
            SetFanRowPlainPercent(Fan2LogicCOMode, Fan2LogicCO);
            SetTwoSwitchRowLabels(Relay1LogicIntro, Relay1LogicRow1, Relay1LogicRow2, Relay1LogicRow3, Relay1LogicRow4, Relay1LogicOC, Relay1LogicCC, isFan: false);
            SetTwoSwitchRowLabels(Relay2LogicIntro, Relay2LogicRow1, Relay2LogicRow2, Relay2LogicRow3, Relay2LogicRow4, Relay2LogicOC, Relay2LogicCC, isFan: false);
        }

        SetSensorPickerItems(Fan1ThermoSensor, _currentMode);
        SetSensorPickerItems(Fan2ThermoSensor, _currentMode);
        SetSensorPickerItems(Relay1ThermoSensor, _currentMode);
        SetSensorPickerItems(Relay2ThermoSensor, _currentMode);

        // A Picker.Items csere nem mindig váltja ki a SelectedIndexChanged eseményt,
        // ezért kézzel is frissítjük a feliratokat/előnézetet/grafikonokat.
        OnFan1ThermoChanged(this, EventArgs.Empty);
        OnFan2ThermoChanged(this, EventArgs.Empty);
        OnRelay1ThermoChanged(this, EventArgs.Empty);
        OnRelay2ThermoChanged(this, EventArgs.Empty);
    }

    private void SetChannelMode(VerticalStackLayout normal, VerticalStackLayout logic, Label tabLabel, bool logicMode)
    {
        normal.IsVisible = !logicMode;
        logic.IsVisible  = logicMode;
        tabLabel.Text = logicMode ? "⚡ Logika" : "🌡 Termosztát";
    }

    // "2 kapcsoló" módban (mode 4) mind a 4 sor (K1×K2 kombináció) használt.
    private void SetTwoSwitchRowLabels(Label intro, Label row1, Label row2, Label row3, Label row4,
        View rowOC, View rowCC, bool isFan)
    {
        row2.IsVisible = true; row4.IsVisible = true;
        rowOC.IsVisible = true; rowCC.IsVisible = true;
        string valueDesc = isFan ? "a ventilátor fordulatszámát (%)" : "húzzon-e a relé";
        intro.Text = $"Add meg K1/K2 minden állapot-kombinációjához {valueDesc}";
        row1.Text = "K1: Nyitott · K2: Nyitott";
        row2.Text = "K1: Nyitott · K2: Zárt";
        row3.Text = "K1: Zárt · K2: Nyitott";
        row4.Text = "K1: Zárt · K2: Zárt";
    }

    // "1 kapcsoló+1 hőmérő" módban (mode 3) csak K1 számít -> 2 sor elég
    // (Row2/Row4 elrejtve). A "Kapcsoló+hőmérséklet" választásnál a görbe is
    // látszik, és az érték mezőkbe "-1" írva a görbe eredménye érvényesül,
    // konkrét 0-100 érték pedig felülbírálja azt az adott K1 állapotra.
    private void ApplyFanControlSource(Picker source, VerticalStackLayout normal, VerticalStackLayout logic,
        Label tabLabel, Label intro, Label row1, Label row2, Label row3, Label row4, Entry rowOC, Entry rowCC)
    {
        int sel = source.SelectedIndex;
        bool showNormal = sel == 0 || sel == 2; // Hőmérséklet vagy Kapcsoló+hőmérséklet
        bool showLogic  = sel == 1 || sel == 2; // Kapcsoló vagy Kapcsoló+hőmérséklet
        normal.IsVisible = showNormal;
        logic.IsVisible  = showLogic;
        row2.IsVisible = false; row4.IsVisible = false;
        rowOC.IsVisible = false; rowCC.IsVisible = false;
        row1.Text = "K1: Nyitott";
        row3.Text = "K1: Zárt";
        intro.Text = "K1 állapotonként válaszd ki, mit csináljon a ventilátor";
        tabLabel.Text = sel switch { 0 => "🌡 Termosztát", 1 => "⚡ Logika", _ => "🌡+⚡ Kombinált" };
    }
    private void ApplyRelayControlSource(Picker source, VerticalStackLayout normal, VerticalStackLayout logic,
        Label tabLabel, Label intro, Label row1, Label row2, Label row3, Label row4, View rowOC, View rowCC)
    {
        int sel = source.SelectedIndex;
        bool showNormal = sel == 0 || sel == 2;
        bool showLogic  = sel == 1 || sel == 2;
        normal.IsVisible = showNormal;
        logic.IsVisible  = showLogic;
        row2.IsVisible = false; row4.IsVisible = false;
        rowOC.IsVisible = false; rowCC.IsVisible = false;
        row1.Text = "K1: Nyitott";
        row3.Text = "K1: Zárt";
        intro.Text = "K1 állapotonként kapcsold BE, ha a relének húznia kell (KI = a normál hőmérséklet-termosztát dönt, ha az is aktív)";
        tabLabel.Text = sel switch { 0 => "🌡 Termosztát", 1 => "⚡ Logika", _ => "🌡+⚡ Kombinált" };
    }

    private void OnFan1ControlSourceChanged(object s, EventArgs e)
    {
        ApplyModeVisibility();
        if (Fan1ControlSource.SelectedIndex == 0 && CheckBLE()) _ = BluetoothManager.WriteAsync("DEL_LOGIC:F1");
    }
    private void OnFan2ControlSourceChanged(object s, EventArgs e)
    {
        ApplyModeVisibility();
        if (Fan2ControlSource.SelectedIndex == 0 && CheckBLE()) _ = BluetoothManager.WriteAsync("DEL_LOGIC:F2");
    }
    private void OnRelay1ControlSourceChanged(object s, EventArgs e)
    {
        ApplyModeVisibility();
        if (Relay1ControlSource.SelectedIndex == 0 && CheckBLE()) _ = BluetoothManager.WriteAsync("DEL_LOGIC:R1");
    }
    private void OnRelay2ControlSourceChanged(object s, EventArgs e)
    {
        ApplyModeVisibility();
        if (Relay2ControlSource.SelectedIndex == 0 && CheckBLE()) _ = BluetoothManager.WriteAsync("DEL_LOGIC:R2");
    }

    // Az "Érzékelő" választó tartalma módtól függ:
    //   0 (2 hőmérő): NTC1, NTC2
    //   1 (1 hőmérő): csak NTC1
    //   2 (páratartalom): Hőmérséklet(NTC1), Páratartalom(%)
    //   3/4: nem releváns (Logika/Kombinált nézet aktív)
    private void SetSensorPickerItems(Picker p, int mode)
    {
        int prevIndex = p.SelectedIndex;
        p.Items.Clear();
        if (mode == 2)
        {
            p.Items.Add("Hőmérséklet (NTC1)");
            p.Items.Add("Páratartalom (%)");
        }
        else if (mode == 1 || mode == 3)
        {
            p.Items.Add("NTC1 (T1)");
        }
        else
        {
            p.Items.Add("NTC1 (T1)");
            p.Items.Add("NTC2 (T2)");
        }
        p.SelectedIndex = prevIndex >= 0 && prevIndex < p.Items.Count ? prevIndex : 0;
    }

    // A firmware felé küldött szenzor-byte: 1=NTC1, 2=NTC2, 3=Páratartalom
    private int SensorByte(Picker p, bool humidityMode)
    {
        if (p.SelectedIndex != 1) return 1;
        return humidityMode ? 3 : 2;
    }

    // ══════════════ KAPCSOLÓ(+HŐMÉRSÉKLET)-LOGIKA (mode 3/4, csatornánként) ══════════════
    // Parancs (mode 4, 2 kapcsoló):      SET_LOGIC:<F1|F2|R1|R2>:<en>:<v_OO>:<v_OC>:<v_CO>:<v_CC>:0
    // Parancs (mode 3, csak K1 számít):  SET_LOGIC:<F1|F2|R1|R2>:<en>:<v_OO>:<v_OO>:<v_CO>:<v_CO>:2
    // A záró mező (axis2Mode) mondja meg a firmware-nek, hogy a 2. bit K2
    // kapcsoló (0) vagy nincs is 2. tengely, csak K1 számít (2).
    // Ventilátornál v = 0-100 (%), VAGY -1 = "a görbe eredménye érvényesüljön".
    // Relénél v = 1 (mindig húzzon) vagy 0 (a hőmérséklet-termosztát dönt, ha az is be van kapcsolva).
    // 2 kapcsoló módban (mode 4) nincs "Görbe szerint" lehetőség - a Mode picker
    // elrejtve, csak az egyszerű %-os mező látszik.
    private static void SetFanRowPlainPercent(Picker mode, Entry entry)
    {
        mode.IsVisible = false;
        entry.IsVisible = true;
    }

    // A Mode Picker (Görbe szerint/Kikapcsolva/Bekapcsolva/Egyéni %) alapján
    // számolt érték: -1=görbe, 0=kikapcsolva, 100=bekapcsolva, egyébként az
    // "Egyéni %" mezőben megadott, 0-100 közé szorított érték.
    // 2 kapcsoló módban (mode 4) nincs görbe opció - egyszerűen a mező %-a számít.
    private int ResolveFanLogicValue(Picker mode, Entry customEntry)
    {
        if (_currentMode == 4) return ClampPct(customEntry.Text);
        return mode.SelectedIndex switch
        {
            1 => 0,
            2 => 100,
            3 => ClampPct(customEntry.Text),
            _ => -1,
        };
    }

    private void OnFan1LogicOOModeChanged(object s, EventArgs e) => Fan1LogicOO.IsVisible = Fan1LogicOOMode.SelectedIndex == 3;
    private void OnFan1LogicCOModeChanged(object s, EventArgs e) => Fan1LogicCO.IsVisible = Fan1LogicCOMode.SelectedIndex == 3;
    private void OnFan2LogicOOModeChanged(object s, EventArgs e) => Fan2LogicOO.IsVisible = Fan2LogicOOMode.SelectedIndex == 3;
    private void OnFan2LogicCOModeChanged(object s, EventArgs e) => Fan2LogicCO.IsVisible = Fan2LogicCOMode.SelectedIndex == 3;

    private async void OnFan1LogicSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan1LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        int oo = ResolveFanLogicValue(Fan1LogicOOMode, Fan1LogicOO);
        int co = ResolveFanLogicValue(Fan1LogicCOMode, Fan1LogicCO);
        int axis2Mode = _currentMode == 3 ? 2 : 0;
        int fOO = ParseFreq(Fan1LogicFreqOO), fOC = ParseFreq(Fan1LogicFreqOC);
        int fCO = ParseFreq(Fan1LogicFreqCO), fCC = ParseFreq(Fan1LogicFreqCC);
        await BluetoothManager.WriteAsync($"SET_LOGIC:F1:1:{oo}:{oo}:{co}:{co}:{axis2Mode}:{fOO}:{fOC}:{fCO}:{fCC}");
        Fan1LogicStatus.Text = "Logika elküldve";
    }
    private async void OnFan1LogicDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan1LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_LOGIC:F1");
        Fan1LogicStatus.Text = "Törölve";
    }
    private async void OnFan2LogicSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan2LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        int oo = ResolveFanLogicValue(Fan2LogicOOMode, Fan2LogicOO);
        int co = ResolveFanLogicValue(Fan2LogicCOMode, Fan2LogicCO);
        int axis2Mode = _currentMode == 3 ? 2 : 0;
        int fOO = ParseFreq(Fan2LogicFreqOO), fOC = ParseFreq(Fan2LogicFreqOC);
        int fCO = ParseFreq(Fan2LogicFreqCO), fCC = ParseFreq(Fan2LogicFreqCC);
        await BluetoothManager.WriteAsync($"SET_LOGIC:F2:1:{oo}:{oo}:{co}:{co}:{axis2Mode}:{fOO}:{fOC}:{fCO}:{fCC}");
        Fan2LogicStatus.Text = "Logika elküldve";
    }
    private async void OnFan2LogicDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan2LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_LOGIC:F2");
        Fan2LogicStatus.Text = "Törölve";
    }

    private static int ParseFreq(Entry e) => int.TryParse(e.Text, out int v) ? Math.Clamp(v, 10, 25000) : 30;
    private async void OnRelay1LogicSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        int oo = RelayLogicPickerValue(Relay1LogicOO);
        int oc = RelayLogicPickerValue(Relay1LogicOC);
        int co = RelayLogicPickerValue(Relay1LogicCO);
        int cc = RelayLogicPickerValue(Relay1LogicCC);
        int axis2Mode = _currentMode == 3 ? 2 : 0;
        if (_currentMode == 3)
            await BluetoothManager.WriteAsync($"SET_LOGIC:R1:1:{oo}:{oo}:{co}:{co}:{axis2Mode}");
        else
            await BluetoothManager.WriteAsync($"SET_LOGIC:R1:1:{oo}:{oc}:{co}:{cc}:{axis2Mode}");
        Relay1LogicStatus.Text = "Logika elküldve";
    }
    private async void OnRelay1LogicDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_LOGIC:R1");
        Relay1LogicStatus.Text = "Törölve";
    }
    private async void OnRelay2LogicSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        int oo = RelayLogicPickerValue(Relay2LogicOO);
        int oc = RelayLogicPickerValue(Relay2LogicOC);
        int co = RelayLogicPickerValue(Relay2LogicCO);
        int cc = RelayLogicPickerValue(Relay2LogicCC);
        int axis2Mode = _currentMode == 3 ? 2 : 0;
        if (_currentMode == 3)
            await BluetoothManager.WriteAsync($"SET_LOGIC:R2:1:{oo}:{oo}:{co}:{co}:{axis2Mode}");
        else
            await BluetoothManager.WriteAsync($"SET_LOGIC:R2:1:{oo}:{oc}:{co}:{cc}:{axis2Mode}");
        Relay2LogicStatus.Text = "Logika elküldve";
    }
    private async void OnRelay2LogicDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2LogicStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await BluetoothManager.WriteAsync("DEL_LOGIC:R2");
        Relay2LogicStatus.Text = "Törölve";
    }

    // ══════════════ IDŐ ALAPÚ RELÉKAPCSOLÁS (RHOLD) ══════════════
    private async void OnRelay1HoldSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1HoldStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await SendRHold("R1", Relay1HoldEnabled, Relay1HoldDuration, Relay1HoldLockDuration,
            Relay1HoldMode, Relay1HoldLockMode, Relay1HoldState);
        Relay1HoldStatus.Text = "Elküldve";
    }
    private async void OnRelay2HoldSave(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2HoldStatus.Text = "Nincs BLE kapcsolat!"; return; }
        await SendRHold("R2", Relay2HoldEnabled, Relay2HoldDuration, Relay2HoldLockDuration,
            Relay2HoldMode, Relay2HoldLockMode, Relay2HoldState);
        Relay2HoldStatus.Text = "Elküldve";
    }

    private async Task SendRHold(string target, Microsoft.Maui.Controls.Switch enabledSw, Entry durationEntry, Entry lockDurationEntry,
        Picker modePicker, Picker lockModePicker, Picker statePicker)
    {
        int en = enabledSw.IsToggled ? 1 : 0;
        int duration = int.TryParse(durationEntry.Text, out int d) ? Math.Clamp(d, 1, 36000) : 30;
        int lockDuration = int.TryParse(lockDurationEntry.Text, out int ld) ? Math.Clamp(ld, 0, 36000) : 300;
        int mode = Math.Max(modePicker.SelectedIndex, 0);
        int lockMode = Math.Max(lockModePicker.SelectedIndex, 0);
        int state = Math.Max(statePicker.SelectedIndex, 0);
        await BluetoothManager.WriteAsync($"SET_RHOLD:{target}:{en}:{duration}:{lockDuration}:{mode}:{lockMode}:{state}");
    }

    // LOAD_RHOLD:<target>:<en>:<onDurationSec>:<cooldownSec>:<timerMode>:<lockMode>:<stateFilter>
    private void ParseRHold(string raw)
    {
        var p = raw.Split(':');
        if (p.Length < 7) return;
        string target = p[0];
        bool en = p[1] == "1";
        int duration = int.TryParse(p[2], out int d) ? d : 30;
        int lockDuration = int.TryParse(p[3], out int ld) ? ld : 300;
        int mode = int.TryParse(p[4], out int m) ? m : 0;
        int lockMode = int.TryParse(p[5], out int lm) ? lm : 1;
        int state = int.TryParse(p[6], out int st) ? st : 2;

        if (target == "R1")
            ApplyRHoldToUi(Relay1HoldEnabled, Relay1HoldDuration, Relay1HoldLockDuration, Relay1HoldMode, Relay1HoldLockMode, Relay1HoldState, en, duration, lockDuration, mode, lockMode, state);
        else if (target == "R2")
            ApplyRHoldToUi(Relay2HoldEnabled, Relay2HoldDuration, Relay2HoldLockDuration, Relay2HoldMode, Relay2HoldLockMode, Relay2HoldState, en, duration, lockDuration, mode, lockMode, state);
    }

    private static void ApplyRHoldToUi(Microsoft.Maui.Controls.Switch enabledSw, Entry durationEntry, Entry lockDurationEntry,
        Picker modePicker, Picker lockModePicker, Picker statePicker,
        bool en, int duration, int lockDuration, int mode, int lockMode, int state)
    {
        enabledSw.IsToggled = en;
        durationEntry.Text = duration.ToString();
        lockDurationEntry.Text = lockDuration.ToString();
        if (mode >= 0 && mode < modePicker.Items.Count) modePicker.SelectedIndex = mode;
        if (lockMode >= 0 && lockMode < lockModePicker.Items.Count) lockModePicker.SelectedIndex = lockMode;
        if (state >= 0 && state < statePicker.Items.Count) statePicker.SelectedIndex = state;
    }

    private void ParseLogic(string raw)
    {
        // <F1|F2|R1|R2>:<en>:<v_OO>:<v_OC>:<v_CO>:<v_CC>[:<axis2Mode>][:<f_OO>:<f_OC>:<f_CO>:<f_CC>]
        var p = raw.Split(':');
        if (p.Length < 6) return;
        string target = p[0];
        int oo = int.TryParse(p[2], out int vo) ? vo : -1;
        int oc = int.TryParse(p[3], out int voc) ? voc : -1;
        int co = int.TryParse(p[4], out int vco) ? vco : -1;
        int cc = int.TryParse(p[5], out int vcc) ? vcc : -1;

        switch (target)
        {
            case "F1":
                if (_currentMode == 4) { Fan1LogicOO.Text = Math.Clamp(oo, 0, 100).ToString(); Fan1LogicCO.Text = Math.Clamp(co, 0, 100).ToString(); }
                else { ApplyFanLogicValue(oo, Fan1LogicOOMode, Fan1LogicOO); ApplyFanLogicValue(co, Fan1LogicCOMode, Fan1LogicCO); }
                if (p.Length >= 11)
                {
                    Fan1LogicFreqOO.Text = p[7]; Fan1LogicFreqOC.Text = p[8];
                    Fan1LogicFreqCO.Text = p[9]; Fan1LogicFreqCC.Text = p[10];
                }
                break;
            case "F2":
                if (_currentMode == 4) { Fan2LogicOO.Text = Math.Clamp(oo, 0, 100).ToString(); Fan2LogicCO.Text = Math.Clamp(co, 0, 100).ToString(); }
                else { ApplyFanLogicValue(oo, Fan2LogicOOMode, Fan2LogicOO); ApplyFanLogicValue(co, Fan2LogicCOMode, Fan2LogicCO); }
                if (p.Length >= 11)
                {
                    Fan2LogicFreqOO.Text = p[7]; Fan2LogicFreqOC.Text = p[8];
                    Fan2LogicFreqCO.Text = p[9]; Fan2LogicFreqCC.Text = p[10];
                }
                break;
            case "R1":
                SetRelayLogicPicker(Relay1LogicOO, oo); SetRelayLogicPicker(Relay1LogicOC, oc);
                SetRelayLogicPicker(Relay1LogicCO, co); SetRelayLogicPicker(Relay1LogicCC, cc);
                break;
            case "R2":
                SetRelayLogicPicker(Relay2LogicOO, oo); SetRelayLogicPicker(Relay2LogicOC, oc);
                SetRelayLogicPicker(Relay2LogicCO, co); SetRelayLogicPicker(Relay2LogicCC, cc);
                break;
        }
    }

    // v: -1="Termosztátot követi" (index2), 0="KI" (index0), 1(vagy bármi !=0)="BE" (index1)
    private static void SetRelayLogicPicker(Picker p, int v)
    {
        p.SelectedIndex = v < 0 ? 2 : (v != 0 ? 1 : 0);
    }

    // A Picker kiválasztott indexéből a firmware felé küldendő érték:
    // index0="KI"->0, index1="BE"->1, index2="Termosztátot követi"->-1
    private static int RelayLogicPickerValue(Picker p)
        => p.SelectedIndex switch { 1 => 1, 2 => -1, _ => 0 };

    // Visszatöltéskor a firmware-ből kapott értékből (-1/0/100/egyéni) beállítja
    // a megfelelő Mode picker opciót + szükség esetén az "Egyéni %" mezőt.
    private static void ApplyFanLogicValue(int v, Picker mode, Entry customEntry)
    {
        if (v < 0)        { mode.SelectedIndex = 0; customEntry.IsVisible = false; }
        else if (v == 0)  { mode.SelectedIndex = 1; customEntry.IsVisible = false; }
        else if (v == 100){ mode.SelectedIndex = 2; customEntry.IsVisible = false; }
        else              { mode.SelectedIndex = 3; customEntry.Text = v.ToString(); customEntry.IsVisible = true; }
    }

    private static int ClampPct(string text)
        => int.TryParse(text, out int v) ? Math.Clamp(v, 0, 100) : 0;

    // ── Időzítő törlések ──
    private async void OnFan1TimerDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan1TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        _fan1Slots.Clear();
        RedrawFanSlots(_fan1Slots, Fan1SlotsContainer, Fan1AddSlotBtn);
        await BluetoothManager.WriteAsync("DEL_FTIMER:F1");
        Fan1TimerStatus.Text = "Törölve";
    }
    private async void OnFan2TimerDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Fan2TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        _fan2Slots.Clear();
        RedrawFanSlots(_fan2Slots, Fan2SlotsContainer, Fan2AddSlotBtn);
        await BluetoothManager.WriteAsync("DEL_FTIMER:F2");
        Fan2TimerStatus.Text = "Törölve";
    }
    private async void OnRelay1TimerDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay1TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        _relay1Slots.Clear();
        RedrawRelaySlots(_relay1Slots, Relay1SlotsContainer, Relay1AddSlotBtn);
        await BluetoothManager.WriteAsync("DEL_RTIMER:R1");
        Relay1TimerStatus.Text = "Törölve";
    }
    private async void OnRelay2TimerDelete(object s, EventArgs e)
    {
        if (!CheckBLE()) { Relay2TimerStatus.Text = "Nincs BLE kapcsolat!"; return; }
        _relay2Slots.Clear();
        RedrawRelaySlots(_relay2Slots, Relay2SlotsContainer, Relay2AddSlotBtn);
        await BluetoothManager.WriteAsync("DEL_RTIMER:R2");
        Relay2TimerStatus.Text = "Törölve";
    }

    // ══════════════ BLE FOGADÁS (GET_ALL válasz) ══════════════
    private void OnTimingDataReceived(string data)
    {
        if (string.IsNullOrWhiteSpace(data)) return;
        MainThread.BeginInvokeOnMainThread(() =>
        {
            try
            {
                // SET_FTIMER visszajelzés: FTIMER_OK
                // Adatok betöltése: LOAD_FTIMER:F1:<days>:<H>:<M>:<pct>|...
                if (data.StartsWith("LOAD_FTIMER:"))
                    ParseFanTimer(data.Substring(12));
                else if (data.StartsWith("LOAD_RTIMER:"))
                    ParseRelayTimer(data.Substring(12));
                else if (data.StartsWith("LOAD_THERM:"))
                    ParseTherm(data.Substring(11));
                else if (data.StartsWith("LOAD_LOGIC:"))
                    ParseLogic(data.Substring(11));
                else if (data.StartsWith("LOAD_RHOLD:"))
                    ParseRHold(data.Substring(11));
                else if (data.StartsWith("STATE:"))
                {
                    if (int.TryParse(data.Substring(6), out int m))
                    {
                        _currentMode = m;
                        ApplyModeVisibility();
                    }
                }
            }
            catch (Exception ex) { Debug.WriteLine($"Timing parse: {ex.Message}"); }
        });
    }

    private void ParseFanTimer(string raw)
    {
        // F1:<days>:<H>:<M>:<pct>|<H>:<M>:<pct>
        var parts = raw.Split(':');
        if (parts.Length < 2) return;
        string target = parts[0];
        if (!int.TryParse(parts[1], out int days)) return;

        var slots = new List<TimerSlot>();
        if (parts.Length > 2)
        {
            string slotData = string.Join(":", parts[2..]);
            foreach (var s in slotData.Split('|'))
            {
                var sp = s.Split(':');
                if (sp.Length == 3 &&
                    int.TryParse(sp[0], out int h) &&
                    int.TryParse(sp[1], out int m) &&
                    int.TryParse(sp[2], out int v))
                    slots.Add(new TimerSlot { Hour = h, Minute = m, Value = v });
            }
        }

        if (target == "F1")
        {
            _fan1Days = days; _fan1Slots.Clear(); _fan1Slots.AddRange(slots);
            RefreshDayUI(_fan1DayBorders, _fan1Days);
            RedrawFanSlots(_fan1Slots, Fan1SlotsContainer, Fan1AddSlotBtn);
        }
        else if (target == "F2")
        {
            _fan2Days = days; _fan2Slots.Clear(); _fan2Slots.AddRange(slots);
            RefreshDayUI(_fan2DayBorders, _fan2Days);
            RedrawFanSlots(_fan2Slots, Fan2SlotsContainer, Fan2AddSlotBtn);
        }
    }

    private void ParseRelayTimer(string raw)
    {
        var parts = raw.Split(':');
        if (parts.Length < 2) return;
        string target = parts[0];
        if (!int.TryParse(parts[1], out int days)) return;

        var slots = new List<TimerSlot>();
        if (parts.Length > 2)
        {
            string slotData = string.Join(":", parts[2..]);
            foreach (var s in slotData.Split('|'))
            {
                var sp = s.Split(':');
                if (sp.Length == 3 &&
                    int.TryParse(sp[0], out int h) &&
                    int.TryParse(sp[1], out int m) &&
                    int.TryParse(sp[2], out int v))
                    slots.Add(new TimerSlot { Hour = h, Minute = m, Value = v, IsRelay = true });
            }
        }

        if (target == "R1")
        {
            _relay1Days = days; _relay1Slots.Clear(); _relay1Slots.AddRange(slots);
            RefreshDayUI(_relay1DayBorders, _relay1Days);
            RedrawRelaySlots(_relay1Slots, Relay1SlotsContainer, Relay1AddSlotBtn);
        }
        else if (target == "R2")
        {
            _relay2Days = days; _relay2Slots.Clear(); _relay2Slots.AddRange(slots);
            RefreshDayUI(_relay2DayBorders, _relay2Days);
            RedrawRelaySlots(_relay2Slots, Relay2SlotsContainer, Relay2AddSlotBtn);
        }
    }

    private void ParseTherm(string raw)
    {
        var p = raw.Split(':');
        if (p.Length < 2) return;
        string target = p[0];
        bool en = p[1] == "1";

        if (target == "F1" || target == "F2")
        {
            // F1:<en>:<sensor>:<count>:<t1>:<p1>:<f1>:...:<tn>:<pn>:<fn>
            if (p.Length < 5) return;
            int sensor = int.TryParse(p[2], out int sv) ? sv : 1;
            int count = int.TryParse(p[3], out int cv) ? Math.Clamp(cv, 1, 5) : 5;
            if (p.Length < 4 + count * 3) return;

            var tE = target == "F1"
                ? new[] { Fan1P1T, Fan1P2T, Fan1P3T, Fan1P4T, Fan1P5T }
                : new[] { Fan2P1T, Fan2P2T, Fan2P3T, Fan2P4T, Fan2P5T };
            var pE = target == "F1"
                ? new[] { Fan1P1Pct, Fan1P2Pct, Fan1P3Pct, Fan1P4Pct, Fan1P5Pct }
                : new[] { Fan2P1Pct, Fan2P2Pct, Fan2P3Pct, Fan2P4Pct, Fan2P5Pct };
            var fE = target == "F1"
                ? new[] { Fan1P1Freq, Fan1P2Freq, Fan1P3Freq, Fan1P4Freq, Fan1P5Freq }
                : new[] { Fan2P1Freq, Fan2P2Freq, Fan2P3Freq, Fan2P4Freq, Fan2P5Freq };
            for (int i = 0; i < count; i++)
            {
                if (float.TryParse(p[4 + i * 3], System.Globalization.NumberStyles.Any,
                        System.Globalization.CultureInfo.InvariantCulture, out float t))
                    tE[i].Text = t.ToString("F1");
                if (int.TryParse(p[5 + i * 3], out int pct))
                    pE[i].Text = pct.ToString();
                if (int.TryParse(p[6 + i * 3], out int freq))
                    fE[i].Text = freq.ToString();
            }
            var sensorPicker = target == "F1" ? Fan1ThermoSensor : Fan2ThermoSensor;
            sensorPicker.SelectedIndex = (sensor == 2 || sensor == 3) ? 1 : 0;

            var pointPicker = target == "F1" ? Fan1PointCount : Fan2PointCount;
            pointPicker.SelectedIndex = count - 1; // ez kiváltja a sorok mutatását/rejtését is

            if (target == "F1" && en) SetTab(Fan1ThermoTab, Fan1TimerTab, Fan1ThermoPanel, Fan1TimerPanel);
            if (target == "F2" && en) SetTab(Fan2ThermoTab, Fan2TimerTab, Fan2ThermoPanel, Fan2TimerPanel);
        }
        else if (target == "R1" || target == "R2")
        {
            // R1:<en>:<ton>:<toff>:<sensor>
            if (p.Length < 5) return;
            if (!float.TryParse(p[2], System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out float a)) return;
            if (!float.TryParse(p[3], System.Globalization.NumberStyles.Any,
                    System.Globalization.CultureInfo.InvariantCulture, out float b)) return;
            int sensor = int.TryParse(p[4], out int sv2) ? sv2 : 1;
            if (target == "R1")
            {
                Relay1TOn.Text = a.ToString("F1"); Relay1TOff.Text = b.ToString("F1");
                Relay1ThermoSensor.SelectedIndex = sensor == 2 ? 1 : 0;
                if (en) SetTab(Relay1ThermoTab, Relay1TimerTab, Relay1ThermoPanel, Relay1TimerPanel);
            }
            else
            {
                Relay2TOn.Text = a.ToString("F1"); Relay2TOff.Text = b.ToString("F1");
                Relay2ThermoSensor.SelectedIndex = sensor == 2 ? 1 : 0;
                if (en) SetTab(Relay2ThermoTab, Relay2TimerTab, Relay2ThermoPanel, Relay2TimerPanel);
            }
        }
    }

    // ══════════════ SZENZOR FRISSÍTÉS (MainPage hívja) ══════════════
    public void UpdateSensors(float ntc1, float ntc2)
    {
        Ntc1 = ntc1; Ntc2 = ntc2;
        if (Fan1ThermoPanel.IsVisible)   UpdateFanPreview(Fan1ThermoSensor,
            new[] { Fan1P1T, Fan1P2T, Fan1P3T, Fan1P4T, Fan1P5T },
            new[] { Fan1P1Pct, Fan1P2Pct, Fan1P3Pct, Fan1P4Pct, Fan1P5Pct },
            new[] { Fan1P1Freq, Fan1P2Freq, Fan1P3Freq, Fan1P4Freq, Fan1P5Freq },
            Fan1ThermoPreview, Fan1PointCount, _fan1CurveDraw, Fan1CurveGraph);
        if (Fan2ThermoPanel.IsVisible)   UpdateFanPreview(Fan2ThermoSensor,
            new[] { Fan2P1T, Fan2P2T, Fan2P3T, Fan2P4T, Fan2P5T },
            new[] { Fan2P1Pct, Fan2P2Pct, Fan2P3Pct, Fan2P4Pct, Fan2P5Pct },
            new[] { Fan2P1Freq, Fan2P2Freq, Fan2P3Freq, Fan2P4Freq, Fan2P5Freq },
            Fan2ThermoPreview, Fan2PointCount, _fan2CurveDraw, Fan2CurveGraph);

        bool r1Humidity = IsHumiditySelected(Relay1ThermoSensor);
        bool r2Humidity = IsHumiditySelected(Relay2ThermoSensor);
        float r1T = Relay1ThermoSensor.SelectedIndex == 1 ? ntc2 : ntc1;
        float r2T = Relay2ThermoSensor.SelectedIndex == 1 ? ntc2 : ntc1;
        if (Relay1ThermoPanel.IsVisible)
        {
            // Páratartalom módban nincs bekötött szenzor -> nincs élő adat
            Relay1CurrentTemp.Text = r1Humidity ? "-- %" : $"{r1T:F1} °C";
            _relay1ThermoDraw.CurrentTemp = r1Humidity ? float.NaN : r1T;
            Relay1ThermoGraph.Invalidate();
        }
        if (Relay2ThermoPanel.IsVisible)
        {
            Relay2CurrentTemp.Text = r2Humidity ? "-- %" : $"{r2T:F1} °C";
            _relay2ThermoDraw.CurrentTemp = r2Humidity ? float.NaN : r2T;
            Relay2ThermoGraph.Invalidate();
        }
    }

    // ══════════════ HELPEREK ══════════════
    private static bool F(string text, out float val)
        => float.TryParse(text?.Replace(',', '.'),
               System.Globalization.NumberStyles.Any,
               System.Globalization.CultureInfo.InvariantCulture, out val);

    private static bool CheckBLE() => BluetoothManager.IsConnected;

    private static string BuildFanTimerCmd(string target, int days, List<TimerSlot> slots)
    {
        var slotParts = slots.ConvertAll(s => $"{s.Hour:D2}:{s.Minute:D2}:{s.Value}");
        return $"SET_FTIMER:{target}:{days}:{string.Join("|", slotParts)}";
    }

    private static string BuildRelayTimerCmd(string target, int days, List<TimerSlot> slots)
    {
        var slotParts = slots.ConvertAll(s => $"{s.Hour:D2}:{s.Minute:D2}:{s.Value}");
        return $"SET_RTIMER:{target}:{days}:{string.Join("|", slotParts)}";
    }
}

// ── MAUI helper: Grid.Column beállítás fluent módon ──
internal static class GridExtensions
{
    internal static T AssignColumn<T>(this T view, int col) where T : View
    {
        Grid.SetColumn(view, col);
        return view;
    }
}
