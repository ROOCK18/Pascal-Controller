// Pages/Data.xaml.cs
using System.Diagnostics;
using System.Timers;

namespace Controll.Pages;

public partial class Data : ContentView
{
    private System.Timers.Timer _queryTimer;
    private int  _queryStep   = 0;
    private int  _currentMode = 0;   // 0=2hőm 1=1hőm 2=pára 3=1kap1hőm 4=2kap

    public Data()
    {
        InitializeComponent();
    }

    public void InitializePage()
    {
        BluetoothManager.OnDataReceived -= OnDataReceived;
        BluetoothManager.OnDataReceived += OnDataReceived;

        if (_queryTimer == null)
        {
            _queryTimer = new System.Timers.Timer(1000);
            _queryTimer.Elapsed   += OnQueryTimerElapsed;
            _queryTimer.AutoReset  = true;
        }
        _queryTimer.Start();

        // Első lekérdezés azonnal
        _ = BluetoothManager.WriteAsync("GET_STATE");
    }

    public void StopPage()
    {
        _queryTimer?.Stop();
        BluetoothManager.OnDataReceived -= OnDataReceived;
    }

    // ── Timer: körkörösen kérdezünk ──
    private void OnQueryTimerElapsed(object sender, ElapsedEventArgs e)
    {
        if (!BluetoothManager.IsConnected) return;
        switch (_queryStep)
        {
            case 0: _ = BluetoothManager.WriteAsync("GET_TMP");  break;
            case 1: _ = BluetoothManager.WriteAsync("GET_CTR");  break;
            case 2: _ = BluetoothManager.WriteAsync("GET_RGB");  break;
        }
        _queryStep = (_queryStep + 1) % 3;
    }

    // ── Adatfogadás ──
    private void OnDataReceived(string data)
    {
        if (string.IsNullOrEmpty(data)) return;

        MainThread.BeginInvokeOnMainThread(() =>
        {
            try
            {
                string d = data.Trim();

                // ── Mód / állapot ──
                if (d.StartsWith("STATE:"))
                {
                    if (int.TryParse(d.Substring(6), out int m))
                    { _currentMode = m; ApplyLayoutMode(); }
                    return;
                }

                // ── VER: (verzió — nem kell itt kezelni, Settings kezeli) ──
                if (d.StartsWith("VER:")) return;

                // ── Hőmérséklet / páratartalom / kapcsolók ──
                // Formátum: TMP:<v1>:<v2>
                // v1, v2 lehet:
                //   - hőfok float (°C): "23.4"
                //   - páratartalom int (%): "65"
                //   - kapcsoló állapot: "0" vagy "1"
                if (d.StartsWith("TMP:"))
                {
                    var parts = d.Substring(4).Split(':');
                    if (parts.Length >= 1)
                    {
                        float v1 = ParseF(parts[0]);
                        float v2 = parts.Length >= 2 ? ParseF(parts[1]) : float.NaN;
                        UpdateSensorValues(v1, v2);

                        // Szenzor adatokat a Timing oldalnak is elküldjük (MainPage csinálja,
                        // de ha itt is hozzáférünk a timing-hez, direktben is lehetne)
                    }
                    return;
                }

                // ── Ventilátor / relé állapot ──
                if (d.StartsWith("CTR") && d.Length >= 16)
                {
                    int p1 = Convert.ToInt32(d.Substring(3, 2), 16);
                    int f1 = Convert.ToInt32(d.Substring(5, 4), 16);
                    int p2 = Convert.ToInt32(d.Substring(9, 2), 16);
                    int f2 = Convert.ToInt32(d.Substring(11, 4), 16);
                    int st = Convert.ToInt32(d.Substring(15, 1), 16);

                    Fan1ValueLabel.Text = $"{(int)Math.Round(p1 / 2.55)}%";
                    Fan1FreqLabel.Text  = $"{f1} Hz";
                    Fan2ValueLabel.Text = $"{(int)Math.Round(p2 / 2.55)}%";
                    Fan2FreqLabel.Text  = $"{f2} Hz";

                    UpdateStatusStyle(Fan1Border,   Fan1ValueLabel,  (st & 0x08) != 0);
                    UpdateStatusStyle(Fan2Border,   Fan2ValueLabel,  (st & 0x04) != 0);
                    UpdateStatusStyle(Relay1Border, Relay1Label,     (st & 0x02) != 0);
                    UpdateStatusStyle(Relay2Border, Relay2Label,     (st & 0x01) != 0);

                    // 1 kapcsoló + 1 hőmérő módban a Sensor1 a kapcsoló állapotát mutatja
                    if (_currentMode == 3)
                        UpdateStatusStyle(Sensor1Border, Sensor1Value, (st & 0x02) != 0);
                    // 2 kapcsoló módban mindkét szenzorkártya kapcsoló állapot
                    if (_currentMode == 4)
                    {
                        UpdateStatusStyle(Sensor1Border, Sensor1Value, (st & 0x02) != 0);
                        UpdateStatusStyle(Sensor2Border, Sensor2Value, (st & 0x01) != 0);
                    }
                    return;
                }

                // ── RGB ──
                if (d.Contains("RGB|"))
                {
                    string rgb = d.Substring(d.IndexOf("RGB|")).Trim();
                    var parts = rgb.Split('|');
                    if (parts.Length >= 3)
                    {
                        bool isOn = parts[2] == "1";
                        RgbModeLabel.Text = isOn ? "BEKAPCSOLVA" : "KIKAPCSOLVA";
                        var color = Color.FromArgb(parts[1].StartsWith("#") ? parts[1] : "#" + parts[1]);
                        RgbColorPreview.BackgroundColor = color;
                        int b = (int)(Math.Max(color.Red, Math.Max(color.Green, color.Blue)) * 100);
                        RgbBrightnessLabel.Text = $"{b}%";
                        UpdateStatusStyle(RgbBorder, RgbModeLabel, isOn);
                    }
                }
            }
            catch (Exception ex) { Debug.WriteLine($"Data parse: {ex.Message}"); }
        });
    }

    // ── Módhoz igazodó UI ──
    private void ApplyLayoutMode()
    {
        // Alapállapot visszaállítás
        Sensor2Border.IsVisible = true;
        SensorGrid.ColumnDefinitions[1].Width = new GridLength(1, GridUnitType.Star);
        BottomRelayGrid.IsVisible = true;

        switch (_currentMode)
        {
            case 0: // 2 hőmérő
                SetSensorCard("Hőmérséklet 1", "#FF453A", "Hőmérséklet 2", "#FF453A");
                break;

            case 1: // 1 hőmérő (középen, egy kártya)
                Sensor2Border.IsVisible = false;
                SensorGrid.ColumnDefinitions[1].Width = new GridLength(0);
                SetSensorCard("Hőmérséklet", "#FF453A", "", "");
                break;

            case 2: // DHT11 — hőmérséklet bal, páratartalom jobb
                SetSensorCard("Hőmérséklet", "#FF453A", "Páratartalom", "#FF453A");
                break;

            case 3: // 1 kapcsoló + 1 hőmérő
                // Kapcsoló a bal kártya, hőmérő a jobb
                SetSensorCard("Kapcsoló 1", "White", "Hőmérséklet", "#FF453A");
                break;

            case 4: // 2 kapcsoló
                SetSensorCard("Kapcsoló 1", "White", "Kapcsoló 2", "White");
                break;
        }
    }

    private void SetSensorCard(string t1, string c1, string t2, string c2)
    {
        Sensor1Title.Text = t1;
        Sensor1Value.TextColor = Color.FromArgb(c1);
        if (!string.IsNullOrEmpty(t2))
        {
            Sensor2Title.Text = t2;
            Sensor2Value.TextColor = Color.FromArgb(c2);
        }
    }

    // ── Szenzor értékek kiírása mód szerint ──
    private void UpdateSensorValues(float v1, float v2)
    {
        switch (_currentMode)
        {
            case 0: // 2 hőmérő
                Sensor1Value.Text = float.IsNaN(v1) ? "-- °C" : $"{v1:F1} °C";
                Sensor2Value.Text = float.IsNaN(v2) ? "-- °C" : $"{v2:F1} °C";
                break;

            case 1: // 1 hőmérő
                Sensor1Value.Text = float.IsNaN(v1) ? "-- °C" : $"{v1:F1} °C";
                break;

            case 2: // DHT11: v1=hőmérséklet, v2=páratartalom
                Sensor1Value.Text = float.IsNaN(v1) ? "-- °C" : $"{v1:F1} °C";
                Sensor2Value.Text = float.IsNaN(v2) ? "--%"   : $"{(int)v2}%";
                break;

            case 3: // 1 kapcsoló + 1 hőmérő
                // Kapcsoló állapot a CTR üzenetből jön (ld. fent), hőmérő v1-ből
                Sensor2Value.Text = float.IsNaN(v1) ? "-- °C" : $"{v1:F1} °C";
                break;

            case 4: // 2 kapcsoló — állapot a CTR-ből jön, TMP nem releváns
                break;
        }
    }

    private void UpdateStatusStyle(Border border, Label label, bool isActive)
    {
        border.BackgroundColor = isActive
            ? Color.FromArgb("#FF453A")
            : Color.FromArgb("#3A3A3C");
        label.TextColor = Colors.White;

        if (label == Relay1Label || label == Relay2Label)
            label.Text = isActive ? "ON" : "OFF";

        // Kapcsoló módban a szenzorkártyán is ON/OFF szöveg kell
        if ((_currentMode == 3 || _currentMode == 4) &&
            (label == Sensor1Value || label == Sensor2Value))
        {
            label.Text = isActive ? "ZÁRT" : "NYITOTT";
        }
    }

    private static float ParseF(string s)
    {
        if (float.TryParse(s?.Trim().Replace(',', '.'),
                System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out float v))
            return v;
        return float.NaN;
    }
}
