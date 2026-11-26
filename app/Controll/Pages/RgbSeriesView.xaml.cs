using System.Globalization;
using Controll.Services;

namespace Controll.Pages;

// Az RGB automatikák listás legördülője. Kind:
//   Timer - időzítő: napokra, órára események (be/ki, szín, fényerő, átmenet)       SET_RGBTIMER / LOAD_RGBTIMER
//   Day   - napszak-görbe: napszak szerinti max. fényerő (szín nincs), köztük folyamatos átmenet  SET_RGBDAY / LOAD_RGBDAY
//   Therm - hőmérséklet-görbe: hőmérséklet szerinti pontok, köztük folyamatos átmenet SET_RGBTHERM / LOAD_RGBTHERM
// A beállítás az eszközön tárolódik; az RGB Automata/Kézi kapcsolóval együtt töltődik be (GET_RGBAUTO).
public partial class RgbSeriesView : ContentView
{
    public string Kind { get; set; } = "Timer";   // Timer | Day | Therm

    private sealed class Row
    {
        public Border Frame = null!;
        public Entry? A;                  // csak hőmérséklet-görbe: hőmérséklet
        public TimePicker? Time;          // időzítő / napszak-görbe: görgethető időválasztó
        public Picker? Action;            // csak időzítő: BE / KI
        public VerticalStackLayout? OnBox; // szín + fényerő (időzítő KI műveletnél rejtett)
        public ColorField? Color;         // napszak-görbénél nincs szín
        public Entry Pct = null!;
        public Entry? Ramp;               // csak időzítő: átmenet (mp)
    }

    private readonly List<Row> _rows = new();
    private readonly List<Border> _dayButtons = new();
    private int _days = 0x7F;
    private bool _init;
    private readonly DayCurveDrawable _dayDraw = new();

    private int MaxRows => Kind == "Timer" ? 5 : Kind == "Day" ? 10 : 5;

    public RgbSeriesView()
    {
        InitializeComponent();
        Loaded += (s, e) => Init();
    }

    private void Init()
    {
        if (_init) return;
        _init = true;

        switch (Kind)
        {
            case "Day":
                TitleLabel.Text = "Napszak-görbe";
                IntroLabel.Text = "Max. fényerő a nap folyamán (max. 10 pont). Ha be van kapcsolva, minden automatika fényerejét ez adja; a színt az automatika.";
                DayGraph.IsVisible = true;
                DayGraph.Drawable = _dayDraw;
                AddLabel.Text = "+ Pont hozzáadása";
                SaveLabel.Text = "NAPSZAK-GÖRBE MENTÉSE";
                DaysBox.IsVisible = true;
                break;
            case "Therm":
                TitleLabel.Text = "Hőmérséklet-görbe";
                IntroLabel.Text = "Hőmérséklet szerint változik a szín és a fényerő (max. 5 pont).";
                AddLabel.Text = "+ Pont hozzáadása";
                SaveLabel.Text = "HŐMÉRSÉKLET-GÖRBE MENTÉSE";
                SensorBox.IsVisible = true;
                SensorPicker.SelectedIndex = 0;
                break;
            default:
                TitleLabel.Text = "Időzítő";
                IntroLabel.Text = "Napokra és órára események: be/ki, szín, fényerő, átmeneti idő (max. 5).";
                AddLabel.Text = "+ Slot hozzáadása";
                SaveLabel.Text = "IDŐZÍTŐ MENTÉSE";
                DaysBox.IsVisible = true;
                break;
        }

        if (DaysBox.IsVisible) BuildDayButtons();

        BluetoothManager.OnDataReceived += OnData;
    }

    private void OnHeaderTapped(object? sender, TappedEventArgs e)
    {
        DetailsPanel.IsVisible = !DetailsPanel.IsVisible;
        ExpandIcon.Text = DetailsPanel.IsVisible ? "▲" : "▼";
    }

    // ── Napok ──
    private void BuildDayButtons()
    {
        string[] names = { "H", "K", "SZ", "CS", "P", "SZ", "V" };
        for (int i = 0; i < 7; i++)
        {
            int bit = i;
            var b = new Border
            {
                StrokeShape = new Microsoft.Maui.Controls.Shapes.RoundRectangle { CornerRadius = new CornerRadius(6) },
                Padding = new Thickness(10, 6),
                Content = new Label { Text = names[i], TextColor = Colors.White, FontSize = 13 }
            };
            b.GestureRecognizers.Add(new TapGestureRecognizer
            {
                Command = new Command(() => { _days ^= (1 << bit); UpdateDayButtons(); })
            });
            _dayButtons.Add(b);
            DaysRow.Add(b);
        }
        UpdateDayButtons();
    }

    private void UpdateDayButtons()
    {
        var red = UiHelper.ThemeColor("PrimaryRed");
        for (int i = 0; i < _dayButtons.Count; i++)
            _dayButtons[i].BackgroundColor = ((_days >> i) & 1) != 0 ? red : Color.FromArgb("#444444");
    }

    // ── Sorok ──
    private static Label Small(string text) => new Label { Text = text, TextColor = Colors.Gray, FontSize = 12 };

    private static Entry NewEntry(string text, Keyboard? kb = null, string? placeholder = null) => new Entry
    {
        Text = text, TextColor = Colors.White, BackgroundColor = Color.FromArgb("#3A3A3A"), HeightRequest = 40,
        Keyboard = kb ?? Keyboard.Default, Placeholder = placeholder
    };

    private Row AddRow(string a, int action, string hex, int pct, int ramp)
    {
        var row = new Row();
        var stack = new VerticalStackLayout { Spacing = 6 };

        var head = new Grid { ColumnDefinitions = { new ColumnDefinition(GridLength.Star), new ColumnDefinition(GridLength.Auto) } };
        head.Add(new Label { Text = Kind == "Therm" ? "Pont" : Kind == "Day" ? "Pont" : "Slot", TextColor = Colors.White, FontAttributes = FontAttributes.Bold, VerticalOptions = LayoutOptions.Center }, 0, 0);
        var del = new Label { Text = "✕ Törlés", TextColor = Color.FromArgb("#FF6B6B"), FontSize = 13, VerticalOptions = LayoutOptions.Center };
        del.GestureRecognizers.Add(new TapGestureRecognizer
        {
            Command = new Command(() => { _rows.Remove(row); RowsContainer.Remove(row.Frame); RenumberRows(); RefreshGraph(); })
        });
        head.Add(del, 1, 0);
        stack.Add(head);

        if (Kind == "Therm")
        {
            stack.Add(Small("Hőmérséklet (°C)"));
            row.A = NewEntry(a, Keyboard.Default, "pl. 25");
            stack.Add(row.A);
        }
        else
        {
            stack.Add(Small("Időpont"));
            TimeSpan.TryParse(a, CultureInfo.InvariantCulture, out var ts);
            row.Time = new TimePicker { Time = ts, Format = "HH:mm", TextColor = Colors.White, BackgroundColor = Color.FromArgb("#3A3A3A") };
            row.Time.PropertyChanged += (s, e) => { if (e.PropertyName == nameof(TimePicker.Time)) RefreshGraph(); };
            stack.Add(row.Time);
        }

        if (Kind == "Timer")
        {
            stack.Add(Small("Művelet"));
            row.Action = new Picker { TextColor = Colors.White, BackgroundColor = Color.FromArgb("#3A3A3A") };
            row.Action.Items.Add("BE");
            row.Action.Items.Add("KI");
            row.Action.SelectedIndex = action == 0 ? 1 : 0;
            stack.Add(row.Action);
        }

        // Szín + fényerő: időzítőnél csak BE műveletnél látszik (KI-nél felesleges)
        var onBox = new VerticalStackLayout { Spacing = 6 };
        if (Kind != "Day")
        {
            onBox.Add(Small("Szín"));
            row.Color = new ColorField { Hex = hex };
            onBox.Add(row.Color);
        }

        onBox.Add(Small(Kind == "Day" ? "Max. fényerő (%)" : "Fényerő (%)"));
        row.Pct = NewEntry(pct.ToString(CultureInfo.InvariantCulture), Keyboard.Numeric);
        row.Pct.TextChanged += (s, e) => RefreshGraph();
        onBox.Add(row.Pct);
        stack.Add(onBox);
        row.OnBox = onBox;
        if (row.Action != null)
        {
            onBox.IsVisible = row.Action.SelectedIndex == 0;
            row.Action.SelectedIndexChanged += (s, e) => onBox.IsVisible = row.Action.SelectedIndex == 0;
        }

        if (Kind == "Timer")
        {
            stack.Add(Small("Átmeneti idő (mp) - BE: felfutás, KI: lefutás"));
            row.Ramp = NewEntry(ramp.ToString(CultureInfo.InvariantCulture), Keyboard.Numeric);
            stack.Add(row.Ramp);
        }

        row.Frame = new Border
        {
            BackgroundColor = Color.FromArgb("#1F1F1F"), Padding = new Thickness(10),
            StrokeShape = new Microsoft.Maui.Controls.Shapes.RoundRectangle { CornerRadius = new CornerRadius(10) },
            Stroke = Colors.Transparent, Content = stack
        };
        _rows.Add(row);
        RowsContainer.Add(row.Frame);
        RefreshGraph();
        return row;
    }

    // A napszak-görbe grafikonja a sorokból frissül (élőben)
    private void RefreshGraph()
    {
        if (Kind != "Day") return;
        _dayDraw.Points.Clear();
        foreach (var r in _rows)
        {
            if (r.Time == null || !int.TryParse((r.Pct.Text ?? "").Trim(), out int pct)) continue;
            var ts = r.Time.Time ?? TimeSpan.Zero;
            _dayDraw.Points.Add(((int)ts.TotalMinutes, Math.Clamp(pct, 0, 100)));
        }
        _dayDraw.NowMinute = DateTime.Now.Hour * 60 + DateTime.Now.Minute;
        DayGraph.Invalidate();
    }

    private void RenumberRows() { /* a sorok sorszámozása nincs kiírva; a törlés után nincs teendő */ }

    private void ClearRows()
    {
        _rows.Clear();
        RowsContainer.Clear();
        RefreshGraph();
    }

    private async void OnAddTapped(object? sender, TappedEventArgs e)
    {
        if (_rows.Count >= MaxRows)
        {
            await Shell.Current.CurrentPage.DisplayAlertAsync("Elérted a maximumot", $"Legfeljebb {MaxRows} sor adható meg.", "OK");
            return;
        }
        if (Kind == "Therm") AddRow("25", 1, "FFFFFF", 100, 0);
        else if (Kind == "Day") AddRow("18:00", 1, "FFFFFF", 100, 0);
        else AddRow("18:00", 1, "FFFFFF", 100, 5);
    }

    // ── Betöltés az eszközről ──
    private static string Hm(string h, string m) => $"{int.Parse(h, CultureInfo.InvariantCulture):D2}:{int.Parse(m, CultureInfo.InvariantCulture):D2}";

    private void OnData(string data)
    {
        if (string.IsNullOrEmpty(data)) return;
        string prefix = Kind == "Day" ? "LOAD_RGBDAY:" : Kind == "Therm" ? "LOAD_RGBTHERM:" : "LOAD_RGBTIMER:";
        if (!data.StartsWith(prefix)) return;
        string body = data.Substring(prefix.Length).Trim();
        MainThread.BeginInvokeOnMainThread(() =>
        {
            try { ApplyLoaded(body); } catch { }
        });
    }

    private void ApplyLoaded(string body)
    {
        var parts = body.Split('|');
        var head = parts[0].Split(':');
        if (head.Length < 2) return;
        EnabledSwitch.IsToggled = head[0] == "1";
        if (Kind == "Therm") SensorPicker.SelectedIndex = head[1] == "2" ? 1 : 0;
        else { _days = int.Parse(head[1], CultureInfo.InvariantCulture); UpdateDayButtons(); }

        ClearRows();
        for (int i = 1; i < parts.Length; i++)
        {
            var t = parts[i].Split(':');
            if (Kind == "Timer" && t.Length >= 6)
                AddRow(Hm(t[0], t[1]), t[2] == "1" ? 1 : 0, t[3], int.Parse(t[4], CultureInfo.InvariantCulture), int.Parse(t[5], CultureInfo.InvariantCulture));
            else if (Kind == "Day" && t.Length >= 3)
                AddRow(Hm(t[0], t[1]), 1, "FFFFFF", int.Parse(t[t.Length - 1], CultureInfo.InvariantCulture), 0);
            else if (Kind == "Therm" && t.Length >= 3)
                AddRow(t[0], 1, t[1], int.Parse(t[2], CultureInfo.InvariantCulture), 0);
        }
    }

    // ── Mentés ──
    private async void OnSaveTapped(object? sender, TappedEventArgs e)
    {
        var page = Shell.Current.CurrentPage;
        if (!BluetoothManager.IsConnected)
        {
            await page.DisplayAlertAsync("Nincs kapcsolat", "A mentéshez csatlakozz az eszközhöz.", "OK");
            return;
        }

        var sb = new System.Text.StringBuilder();
        string en = EnabledSwitch.IsToggled ? "1" : "0";
        string cmd = Kind == "Day" ? "SET_RGBDAY" : Kind == "Therm" ? "SET_RGBTHERM" : "SET_RGBTIMER";
        sb.Append(cmd).Append(':').Append(en).Append(':');
        sb.Append(Kind == "Therm" ? (SensorPicker.SelectedIndex == 1 ? "2" : "1") : _days.ToString(CultureInfo.InvariantCulture));

        int n = 0;
        foreach (var r in _rows)
        {
            n++;
            bool isOff = r.Action != null && r.Action.SelectedIndex == 1;   // KI: szín/fényerő nem számít
            int pct = 100;
            if (!isOff && (!int.TryParse((r.Pct.Text ?? "").Trim(), out pct) || pct < 0 || pct > 100))
            {
                await page.DisplayAlertAsync("Hibás érték", $"{n}. sor: a fényerő 0 és 100 közötti szám legyen.", "OK");
                return;
            }
            string hex = r.Color?.Hex ?? "FFFFFF";
            if (Kind == "Therm")
            {
                if (!double.TryParse((r.A!.Text ?? "").Trim().Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out double temp) || temp < -50 || temp > 150)
                {
                    await page.DisplayAlertAsync("Hibás érték", $"{n}. sor: a hőmérséklet -50 és 150 °C közötti szám legyen.", "OK");
                    return;
                }
                sb.Append('|').Append(temp.ToString("0.#", CultureInfo.InvariantCulture)).Append(':').Append(hex).Append(':').Append(pct);
                continue;
            }
            var tsel = r.Time!.Time ?? TimeSpan.Zero;
            int h = tsel.Hours, m = tsel.Minutes;
            if (Kind == "Day")
            {
                sb.Append('|').Append(h).Append(':').Append(m).Append(':').Append(pct);
                continue;
            }
            if (!int.TryParse((r.Ramp!.Text ?? "").Trim(), out int ramp) || ramp < 0 || ramp > 3600)
            {
                await page.DisplayAlertAsync("Hibás érték", $"{n}. sor: az átmeneti idő 0 és 3600 mp közötti szám legyen.", "OK");
                return;
            }
            int on = r.Action!.SelectedIndex == 1 ? 0 : 1;
            sb.Append('|').Append(h).Append(':').Append(m).Append(':').Append(on).Append(':').Append(hex).Append(':').Append(pct).Append(':').Append(ramp);
        }

        await BluetoothManager.WriteAsync(sb.ToString());
        await page.DisplayAlertAsync("Siker", "RGB automatika elmentve.", "OK");
    }
}
