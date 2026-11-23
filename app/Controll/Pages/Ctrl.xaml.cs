using Microsoft.Maui.Controls;
using System.Diagnostics;
using Controll.Services;

namespace Controll.Pages
{
    public partial class Ctrl : ContentView
    {
        // Állapotváltozók
        private bool _relay1On = false;
        private bool _relay2On = false;
        private bool _fan1On = false;
        private bool _fan2On = false;
        private int _fan1Value = 0;
        private int _fan2Value = 0;
        private int _fan1Freq = 30;
        private int _fan2Freq = 30;

        // Auto/Kézi állapot csatornánként (true=Automata, false=Kézi felülbírálás)
        private bool _fan1Auto = true;
        private bool _fan2Auto = true;
        private bool _relay1Auto = true;
        private bool _relay2Auto = true;

        // Szinkronizációs és torlódásgátló flag-ek
        private bool _isSyncing = false;
        private DateTime _lastFanSentTime = DateTime.MinValue;
        private const int SendThresholdMs = 150; // Minimum ennyi idő kell két csúszka-küldés között

        public Ctrl()
        {
            InitializeComponent();
            UpdateAutoVisual(_fan1Auto, Fan1AutoBorder, Fan1AutoLabel);
            UpdateAutoVisual(_fan2Auto, Fan2AutoBorder, Fan2AutoLabel);
            UpdateAutoVisual(_relay1Auto, Relay1AutoBorder, Relay1AutoLabel);
            UpdateAutoVisual(_relay2Auto, Relay2AutoBorder, Relay2AutoLabel);
        }

        // ===== VENTILÁTOR CSÚSZKA KEZELÉS =====

        private void OnFan1ValueChanged(object sender, ValueChangedEventArgs e)
        {
            if (_isSyncing || _fan1Auto) return; // Automata módban a csúszka nem küld semmit

            _fan1Value = (int)e.NewValue;
            Fan1ValueLabel.Text = $"{_fan1Value}%";

            // Throttling: Csak bizonyos időközönként küldünk adatot mozgatás közben
            if ((DateTime.Now - _lastFanSentTime).TotalMilliseconds > SendThresholdMs)
            {
                SendFanCommand();
            }
        }

        private void OnFan2ValueChanged(object sender, ValueChangedEventArgs e)
        {
            if (_isSyncing || _fan2Auto) return;

            _fan2Value = (int)e.NewValue;
            Fan2ValueLabel.Text = $"{_fan2Value}%";

            if ((DateTime.Now - _lastFanSentTime).TotalMilliseconds > SendThresholdMs)
            {
                SendFanCommand();
            }
        }

        private void OnFanSliderDragCompleted(object sender, EventArgs e)
        {
            if (_fan1Auto && _fan2Auto) return;
            // Amikor elengedi a felhasználó, egy utolsó, biztos értéket elküldünk
            SendFanCommand();
        }

        private async void SendFanCommand()
        {
            if (!BluetoothManager.IsConnected || _isSyncing) return;

            _lastFanSentTime = DateTime.Now;

            // PWM értékek skálázása (0-100 -> 0-255) - Automata csatornáknál a
            // meglévő (görbe/logika által beállított) értéket küldjük tovább,
            // hogy ne írjuk felül a másik csatorna kézi állítása közben.
            int pwm1 = (int)(_fan1Value * 2.55);
            int pwm2 = (int)(_fan2Value * 2.55);

            // Parancs összeállítása HEX formátumban (pl. F64A2)
            string command = $"F{pwm1:X2}{pwm2:X2}";
            await BluetoothManager.WriteAsync(command);
        }

        // ===== VENTILÁTOR POWER KAPCSOLÓK =====

        private async void Fan1PowerSwitch(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _fan1Auto) return;
            _fan1On = !_fan1On;

            await UpdateVisualWithAnimation(_fan1On, Fan1PowerBorder, Fan1PowerLabel);
            await BluetoothManager.WriteAsync(_fan1On ? "F1_1" : "F1_0");

            // Bekapcsoláskor azonnal küldjük a csúszka aktuális állását is
            if (_fan1On) { await Task.Delay(50); SendFanCommand(); }
        }

        private async void Fan2PowerSwitch(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _fan2Auto) return;
            _fan2On = !_fan2On;

            await UpdateVisualWithAnimation(_fan2On, Fan2PowerBorder, Fan2PowerLabel);
            await BluetoothManager.WriteAsync(_fan2On ? "F2_1" : "F2_0");

            if (_fan2On) { await Task.Delay(50); SendFanCommand(); }
        }

        // ===== PWM FREKVENCIA BEÁLLÍTÁSOK =====

        private async void Fan1PWM30(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _fan1Auto) return;
            _fan1Freq = 30;
            UpdatePwm1Selection(Fan1PWM30Border);
            await BluetoothManager.WriteAsync("F1P001E");
        }

        private async void Fan1PWM20k(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _fan1Auto) return;
            _fan1Freq = 20000;
            UpdatePwm1Selection(Fan1PWM20kBorder);
            await BluetoothManager.WriteAsync("F1P4E20");
        }

        private async void Fan1PWME(object sender, TappedEventArgs e)
        {
            if (_fan1Auto) return;
            await ShowFrequencyPrompt(1);
        }

        private async void Fan2PWM30(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _fan2Auto) return;
            _fan2Freq = 30;
            UpdatePwm2Selection(Fan2PWM30Border);
            await BluetoothManager.WriteAsync("F2P001E");
        }

        private async void Fan2PWM20k(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _fan2Auto) return;
            _fan2Freq = 20000;
            UpdatePwm2Selection(Fan2PWM20kBorder);
            await BluetoothManager.WriteAsync("F2P4E20");
        }

        private async void Fan2PWME(object sender, TappedEventArgs e)
        {
            if (_fan2Auto) return;
            await ShowFrequencyPrompt(2);
        }

        private async Task ShowFrequencyPrompt(int fanNumber)
        {
            int currentFreq = (fanNumber == 1) ? _fan1Freq : _fan2Freq;
            string result = await Shell.Current.CurrentPage.DisplayPromptAsync(
                "Egyedi frekvencia",
                $"{fanNumber}. ventilátor (10 - 25000 Hz):",
                "OK", "Mégse", currentFreq.ToString(), -1, Keyboard.Numeric);

            if (int.TryParse(result, out int freq))
            {
                freq = Math.Clamp(freq, 10, 25000);
                if (fanNumber == 1) { _fan1Freq = freq; RefreshPwm1Selection(); }
                else { _fan2Freq = freq; RefreshPwm2Selection(); }

                string hexFreq = freq.ToString("X4");
                await BluetoothManager.WriteAsync($"F{fanNumber}P{hexFreq}");
            }
        }

        // ===== RELÉ VEZÉRLÉS =====

        private async void OnRelay1ToggleTapped(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _relay1Auto) return;
            _relay1On = !_relay1On;
            await UpdateVisualWithAnimation(_relay1On, Relay1ToggleBorder, Relay1Label);
            await BluetoothManager.WriteAsync(_relay1On ? "R1_1" : "R1_0");
        }

        private async void OnRelay2ToggleTapped(object sender, TappedEventArgs e)
        {
            if (_isSyncing || _relay2Auto) return;
            _relay2On = !_relay2On;
            await UpdateVisualWithAnimation(_relay2On, Relay2ToggleBorder, Relay2Label);
            await BluetoothManager.WriteAsync(_relay2On ? "R2_1" : "R2_0");
        }

        // ===== ADATFOGADÁS ÉS SZINKRONIZÁCIÓ =====

        public async void InitializePage()
        {
            if (BluetoothManager.IsConnected)
            {
                BluetoothManager.OnDataReceived -= OnCTRDataReceived;
                BluetoothManager.OnDataReceived += OnCTRDataReceived;
                await BluetoothManager.WriteAsync("GET_CTR");
            }
        }

        private void OnCTRDataReceived(string data)
        {
            if (string.IsNullOrEmpty(data) || !data.StartsWith("CTR")) return;

            MainThread.BeginInvokeOnMainThread(async () =>
            {
                try
                {
                    if (data.Length < 16) return;

                    _isSyncing = true; // Megakadályozza a Slider visszaküldését

                    // Adatok kinyerése a HEX sorból
                    string f1PwmHex = data.Substring(3, 2);
                    string f1FreqHex = data.Substring(5, 4);
                    string f2PwmHex = data.Substring(9, 2);
                    string f2FreqHex = data.Substring(11, 4);
                    string statesHex = data.Substring(15, 1);

                    _fan1Value = (int)Math.Round(Convert.ToInt32(f1PwmHex, 16) / 2.55);
                    _fan1Freq = Convert.ToInt32(f1FreqHex, 16);
                    _fan2Value = (int)Math.Round(Convert.ToInt32(f2PwmHex, 16) / 2.55);
                    _fan2Freq = Convert.ToInt32(f2FreqHex, 16);

                    int states = Convert.ToInt32(statesHex, 16);
                    _fan1On = (states & 0x08) != 0;
                    _fan2On = (states & 0x04) != 0;
                    _relay1On = (states & 0x02) != 0;
                    _relay2On = (states & 0x01) != 0;

                    // Auto/Kézi állapotok (17. karakter, ha a firmware küldi - régebbi
                    // firmware verzióknál hiányozhat, ilyenkor a jelenlegi állapot marad)
                    if (data.Length >= 17)
                    {
                        int autoFlags = Convert.ToInt32(data.Substring(16, 1), 16);
                        _fan1Auto   = (autoFlags & 0x08) != 0;
                        _fan2Auto   = (autoFlags & 0x04) != 0;
                        _relay1Auto = (autoFlags & 0x02) != 0;
                        _relay2Auto = (autoFlags & 0x01) != 0;
                        UpdateAutoVisual(_fan1Auto, Fan1AutoBorder, Fan1AutoLabel);
                        UpdateAutoVisual(_fan2Auto, Fan2AutoBorder, Fan2AutoLabel);
                        UpdateAutoVisual(_relay1Auto, Relay1AutoBorder, Relay1AutoLabel);
                        UpdateAutoVisual(_relay2Auto, Relay2AutoBorder, Relay2AutoLabel);
                    }

                    // UI frissítés
                    Fan1Slider.Value = _fan1Value;
                    Fan2Slider.Value = _fan2Value;
                    Fan1ValueLabel.Text = $"{_fan1Value}%";
                    Fan2ValueLabel.Text = $"{_fan2Value}%";

                    _ = UpdateVisualWithAnimation(_fan1On, Fan1PowerBorder, Fan1PowerLabel);
                    _ = UpdateVisualWithAnimation(_fan2On, Fan2PowerBorder, Fan2PowerLabel);
                    _ = UpdateVisualWithAnimation(_relay1On, Relay1ToggleBorder, Relay1Label);
                    _ = UpdateVisualWithAnimation(_relay2On, Relay2ToggleBorder, Relay2Label);

                    RefreshPwm1Selection();
                    RefreshPwm2Selection();

                    await Task.Delay(100); // Rövid szünet a szinkron végén
                    _isSyncing = false;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($">>> CTR Parse Hiba: {ex.Message}");
                    _isSyncing = false;
                }
            });
        }

        // ===== AUTO / KÉZI FELÜLBÍRÁLÁS =====

        private async void OnFan1AutoToggle(object sender, TappedEventArgs e)
        {
            _fan1Auto = !_fan1Auto;
            UpdateAutoVisual(_fan1Auto, Fan1AutoBorder, Fan1AutoLabel);
            await BluetoothManager.WriteAsync($"SET_AUTO:F1:{(_fan1Auto ? 1 : 0)}");
        }
        private async void OnFan2AutoToggle(object sender, TappedEventArgs e)
        {
            _fan2Auto = !_fan2Auto;
            UpdateAutoVisual(_fan2Auto, Fan2AutoBorder, Fan2AutoLabel);
            await BluetoothManager.WriteAsync($"SET_AUTO:F2:{(_fan2Auto ? 1 : 0)}");
        }
        private async void OnRelay1AutoToggle(object sender, TappedEventArgs e)
        {
            _relay1Auto = !_relay1Auto;
            UpdateAutoVisual(_relay1Auto, Relay1AutoBorder, Relay1AutoLabel);
            await BluetoothManager.WriteAsync($"SET_AUTO:R1:{(_relay1Auto ? 1 : 0)}");
        }
        private async void OnRelay2AutoToggle(object sender, TappedEventArgs e)
        {
            _relay2Auto = !_relay2Auto;
            UpdateAutoVisual(_relay2Auto, Relay2AutoBorder, Relay2AutoLabel);
            await BluetoothManager.WriteAsync($"SET_AUTO:R2:{(_relay2Auto ? 1 : 0)}");
        }

        private void UpdateAutoVisual(bool isAuto, Border border, Label label)
        {
            var primaryRed = (Color)Application.Current.Resources["PrimaryRed"];
            var lightGray = (Color)Application.Current.Resources["LightGray"];
            border.BackgroundColor = isAuto ? lightGray : primaryRed;
            label.Text = isAuto ? "Automata" : "Kézi";

            // Automata módban a hozzá tartozó kézi vezérlők (csúszka/PWM gombok/
            // relékapcsoló) le vannak tiltva és elhalványítva, hogy egyértelmű
            // legyen: itt most a termosztát/logika dönt, nem a felhasználó.
            if (border == Fan1AutoBorder)
            {
                Fan1Slider.IsEnabled = !isAuto;
                Fan1Slider.Opacity = isAuto ? 0.4 : 1.0;
                Fan1PowerBorder.Opacity = isAuto ? 0.4 : 1.0;
            }
            else if (border == Fan2AutoBorder)
            {
                Fan2Slider.IsEnabled = !isAuto;
                Fan2Slider.Opacity = isAuto ? 0.4 : 1.0;
                Fan2PowerBorder.Opacity = isAuto ? 0.4 : 1.0;
            }
            else if (border == Relay1AutoBorder)
            {
                Relay1ToggleBorder.Opacity = isAuto ? 0.4 : 1.0;
            }
            else if (border == Relay2AutoBorder)
            {
                Relay2ToggleBorder.Opacity = isAuto ? 0.4 : 1.0;
            }
        }

        // ===== VIZUÁLIS SEGÉDMETÓDUSOK =====

        private async Task UpdateVisualWithAnimation(bool isOn, Border border, Label label)
        {
            var primaryRed = (Color)Application.Current.Resources["PrimaryRed"];
            var lightGray = (Color)Application.Current.Resources["LightGray"];

            border.BackgroundColor = isOn ? primaryRed : lightGray;
            label.Text = isOn ? "ON" : "OFF";
            await Task.CompletedTask;
        }

        private void RefreshPwm1Selection() => UpdatePwm1Selection(_fan1Freq == 30 ? Fan1PWM30Border : (_fan1Freq == 20000 ? Fan1PWM20kBorder : Fan1PWMEBorder));
        private void RefreshPwm2Selection() => UpdatePwm2Selection(_fan2Freq == 30 ? Fan2PWM30Border : (_fan2Freq == 20000 ? Fan2PWM20kBorder : Fan2PWMEBorder));

        private void UpdatePwm1Selection(Border selected)
        {
            var pRed = (Color)Application.Current.Resources["PrimaryRed"];
            var lGray = (Color)Application.Current.Resources["LightGray"];
            Fan1PWM30Border.BackgroundColor = lGray; Fan1PWM20kBorder.BackgroundColor = lGray; Fan1PWMEBorder.BackgroundColor = lGray;
            if (selected != null) selected.BackgroundColor = pRed;
        }

        private void UpdatePwm2Selection(Border selected)
        {
            var pRed = (Color)Application.Current.Resources["PrimaryRed"];
            var lGray = (Color)Application.Current.Resources["LightGray"];
            Fan2PWM30Border.BackgroundColor = lGray; Fan2PWM20kBorder.BackgroundColor = lGray; Fan2PWMEBorder.BackgroundColor = lGray;
            if (selected != null) selected.BackgroundColor = pRed;
        }

        private void OnTimingClicked(object sender, EventArgs e)
        {
            if (Shell.Current.CurrentPage is MainPage main) main.ShowTiming();
        }
    }
}