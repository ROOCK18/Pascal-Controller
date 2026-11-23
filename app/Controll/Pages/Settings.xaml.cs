using Plugin.BLE;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text;
using System.IO;
using Microsoft.Maui.ApplicationModel.DataTransfer;
using Controll.Services;

namespace Controll.Pages;

public partial class Settings : ContentPage
{
    private IBluetoothLE _ble;
    private IAdapter _adapter;
    private ObservableCollection<IDevice> _deviceList = new();

    private bool _isUpdating = false;
    private int _otaMtu = 20;
    private readonly object _otaLock = new();
    private TaskCompletionSource<bool> _readyTcs;
    private TaskCompletionSource<bool> _restartTcs;
    private CancellationTokenSource _otaCts;

    private string _currentFirmwareVersion = "";
    private List<FirmwareRelease> _releases = new();
    private bool _deviceWifiConnected = false;

    public Settings()
    {
        InitializeComponent();
        _ble = CrossBluetoothLE.Current;
        _adapter = CrossBluetoothLE.Current.Adapter;
        DevicesListView.ItemsSource = _deviceList;
        BluetoothManager.Initialize(_adapter);
        LoadConnectedDevice();
    }

    // Ha a kapcsolat BÁRMIKOR megszakad (jel megszűnik, eszköz kikapcsol,
    // stb.) - nem csak akkor, ha a felhasználó kézzel bontja -, a Settings
    // oldal is essen vissza pontosan abba az állapotba, mintha sosem lett
    // volna csatlakozva (elrejti az összes "csatlakoztatva" panelt).
    private void OnBleConnectionChanged(bool isConnected)
    {
        MainThread.BeginInvokeOnMainThread(() =>
        {
            if (!isConnected)
            {
                UpdateLocalUI(false, "Kapcsolat megszakadt");
                BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
            }
        });
    }

    private void Adapter_DeviceDiscovered(object sender,
        Plugin.BLE.Abstractions.EventArgs.DeviceEventArgs e)
    {
        if (!_deviceList.Any(d => d.Id == e.Device.Id) &&
            !string.IsNullOrEmpty(e.Device.Name))
            MainThread.BeginInvokeOnMainThread(() => _deviceList.Add(e.Device));
    }

    private async void OnBTSearchClicked(object sender, EventArgs e)
    {
        var status = await CheckBTPerm();
        if (status != PermissionStatus.Granted) return;
        if (!_ble.IsOn) { await DisplayAlert("BT", "Kapcsold be a Bluetooth-t!", "OK"); return; }
        try
        {
            _deviceList.Clear();
            _selectedDeviceRow = null;
            ScanIndicator.IsRunning = true;
            BtnSearch.IsEnabled = false;
            _adapter.DeviceDiscovered += Adapter_DeviceDiscovered;
            _adapter.ScanTimeout = 5000;
            await _adapter.StartScanningForDevicesAsync();
        }
        catch (Exception ex) { await DisplayAlert("Hiba", ex.Message, "OK"); }
        finally
        {
            _adapter.DeviceDiscovered -= Adapter_DeviceDiscovered;
            ScanIndicator.IsRunning = false;
            BtnSearch.IsEnabled = true;
        }
    }

    private Border _selectedDeviceRow;

    private async void OnDeviceRowTapped(object sender, TappedEventArgs e)
    {
        if (sender is not Border border || border.BindingContext is not IDevice device) return;

        // Azonnali, félreérthetetlen vizuális visszajelzés: a megérintett
        // sor kiemelt (piros) színt kap, hogy egyértelmű legyen, MELYIK
        // eszközre koppintottál, amíg a csatlakozás folyamatban van.
        if (_selectedDeviceRow != null && _selectedDeviceRow != border)
        {
            _selectedDeviceRow.BackgroundColor = (Color)Application.Current.Resources["DarkGray"];
            _selectedDeviceRow.Stroke = (Color)Application.Current.Resources["LightGray"];
        }
        _selectedDeviceRow = border;
        border.BackgroundColor = (Color)Application.Current.Resources["PrimaryRed"];
        border.Stroke = (Color)Application.Current.Resources["PrimaryRed"];

        try
        {
            ScanIndicator.IsRunning = true;
            await _adapter.ConnectToDeviceAsync(device);
            BluetoothManager.ConnectedDevice = device;
            BluetoothManager.SaveConnectedDevice(device);

            var services = await device.GetServicesAsync();
            foreach (var svc in services)
            {
                var chars = await svc.GetCharacteristicsAsync();
                foreach (var ch in chars)
                {
                    if (ch.CanWrite) BluetoothManager.WriteCharacteristic = ch;
                    if (ch.CanUpdate || ch.CanRead)
                    {
                        BluetoothManager.ReadCharacteristic = ch;
                        await BluetoothManager.SubscribeToNotifications();
                    }
                }
            }

            int mtu = await BluetoothManager.RequestMtuAsync(517);
            // Minimum 128 byte chunk – 20 byte-os chunkoknál az ESP Update.write()
            // belső bufferjei hibáznak és a sok kis csomag GATT torlódást okoz
            // Max 128 byte: az Android BLE stack 512 byte-os chunkokat feldarabol
            // ami az ESP onWrite-ban partial write-ot okoz és torlódást az MTU határon
            _otaMtu = Math.Max(Math.Min(mtu - 3, 128), 128);
            Debug.WriteLine($"OTA chunk méret: {_otaMtu} (MTU={mtu})");

            BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
            BluetoothManager.OnDataReceived += OnSettingsDataReceived;
            UpdateLocalUI(true, device.Name);
            RefreshSchemePicker();
            BluetoothManager.NotifyConnectionSuccess();
            await SyncTimeAsync();
            await Task.Delay(200);
            await BluetoothManager.WriteAsync("GET_STATE");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_COMP");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_DISPLAY");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_DATEPOS");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_WIFI_STATUS");
            // Nincs felugró "sikeres csatlakozás" ablak - a UI (UpdateLocalUI)
            // már addigra átvált a csatlakoztatott nézetre, ez elég visszajelzés.
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Csatlakozás: {ex.Message}");
            border.BackgroundColor = (Color)Application.Current.Resources["DarkGray"];
            border.Stroke = (Color)Application.Current.Resources["LightGray"];
            _selectedDeviceRow = null;
            await DisplayAlert("Hiba", "Csatlakozás sikertelen!", "OK");
        }
        finally
        {
            ScanIndicator.IsRunning = false;
        }
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        BluetoothManager.OnConnectionChanged -= OnBleConnectionChanged;
        BluetoothManager.OnConnectionChanged += OnBleConnectionChanged;
        LoadConnectedDevice();
        if (BluetoothManager.IsConnected)
        {
            BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
            BluetoothManager.OnDataReceived += OnSettingsDataReceived;
            await BluetoothManager.WriteAsync("GET_STATE");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_COMP");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_DISPLAY");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_DATEPOS");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_WIFI_STATUS");
            RefreshSchemePicker();
        }
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        BluetoothManager.OnConnectionChanged -= OnBleConnectionChanged;
        if (!_isUpdating)
            BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
    }

    private void OnSettingsDataReceived(string data)
    {
        Debug.WriteLine($"[Settings RX]: {data}");
        MainThread.BeginInvokeOnMainThread(() =>
        {
            var d = data.Trim();
            if (d.StartsWith("STATE:") && int.TryParse(d.Substring(6), out int m))
            {
                _suppressModeChangeSend = true;
                ModePicker.SelectedIndex = m;
                _suppressModeChangeSend = false;
                ModeSelectionPanel.IsVisible = true;
            }
            else if (d.StartsWith("VER:"))
            {
                _currentFirmwareVersion = d.Substring(4);
                LblFirmwareVersion.Text = _currentFirmwareVersion;
            }
            else if (d.StartsWith("LOAD_COMP:"))
                ParseLoadComp(d.Substring(10));
            else if (d.StartsWith("LOAD_DISPLAY:"))
                ParseLoadDisplay(d.Substring(13));
            else if (d.StartsWith("LOAD_DATEPOS:"))
                ParseLoadDatePos(d.Substring(13));
            else if (d.StartsWith("WIFI_STATUS:"))
                ParseWifiStatus(d.Substring(12));
            else if (d == "WIFI_CONNECTING")
            {
                LblWifiStatus.Text = "WiFi állapot: csatlakozás...";
                if (_isUpdating) LblUpdateStatus.Text = "WiFi csatlakozás...";
            }
            else if (d.StartsWith("WIFI_CONNECTED:"))
            {
                string ip = d.Substring(15);
                LblWifiStatus.Text = $"WiFi állapot: csatlakozva ({_pendingConnectSsid}, {ip})";
                if (_isUpdating) LblUpdateStatus.Text = $"WiFi csatlakozva ({ip}), letöltés indul...";
                _deviceWifiConnected = true;
                WifiStatusDot.Fill = Colors.LightGreen;
                ApplyWifiViewState(true, false, _pendingConnectSsid);
                _wifiConnectTcs?.TrySetResult("OK");
            }
            else if (d == "WIFI_WRONG_PASSWORD")
                _wifiConnectTcs?.TrySetResult("WRONG_PASSWORD");
            else if (d.StartsWith("WIFI_FAILED:"))
                _wifiConnectTcs?.TrySetResult("FAILED:" + d.Substring(12));
            else if (d.StartsWith("WIFI_SCAN_START:"))
                _scannedNetworks.Clear();
            else if (d.StartsWith("WIFI_NET:"))
            {
                var parts = d.Substring(9).Split(':');
                if (parts.Length >= 2 && int.TryParse(parts[parts.Length - 1], out int rssi))
                {
                    string ssid = string.Join(":", parts, 0, parts.Length - 1); // ha az SSID maga tartalmazna ':'-t
                    if (!_scannedNetworks.Any(n => n.Ssid == ssid))
                        _scannedNetworks.Add((ssid, rssi));
                }
            }
            else if (d == "WIFI_SCAN_DONE")
                _wifiScanTcs?.TrySetResult(true);
            else if (d.StartsWith("OTA_PROGRESS:") && int.TryParse(d.Substring(13), out int wp))
                HandleWifiOtaProgress(wp);
        });
    }

    private void ParseWifiStatus(string body)
    {
        var p = body.Split(':');
        if (p.Length < 1) return;
        int state = int.TryParse(p[0], out int st) ? st : 0; // 0=nincs/nincs SSID, 1=csatlakozva, 2=folyamatban
        string ip = p.Length > 1 ? p[1] : "";
        string ssid = p.Length > 2 ? p[2] : "";
        bool connected = state == 1;
        bool connecting = state == 2;
        _deviceWifiConnected = connected;
        WifiStatusDot.Fill = connected ? Colors.LightGreen : (connecting ? Colors.Orange : Colors.Gray);
        LblWifiStatus.Text = connected
            ? $"WiFi állapot: csatlakozva ({ssid}, {ip})"
            : connecting
                ? $"WiFi állapot: csatlakozás folyamatban... ({ssid})"
                : (string.IsNullOrEmpty(ssid) ? "WiFi állapot: nincs beállítva" : $"WiFi állapot: nincs csatlakozva ({ssid})");
        ApplyWifiViewState(connected, connecting, ssid);
        _wifiStatusRefreshTcs?.TrySetResult(true);
    }

    // Ha van MENTETT hálózat (akár csatlakozva, akár épp csatlakozás
    // folyamatban), nem lehet újat keresni - csak a Bontás+Elfelejtés gomb
    // látszik. Ha nincs mentett hálózat, a keresés/csatlakozás UI látszik.
    private void ApplyWifiViewState(bool connected, bool connecting, string ssid)
    {
        bool hasSavedNetwork = connected || connecting || !string.IsNullOrEmpty(ssid);
        WifiConnectedView.IsVisible = hasSavedNetwork;
        WifiDisconnectedView.IsVisible = !hasSavedNetwork;
        if (hasSavedNetwork)
        {
            LblWifiConnectedSsid.Text = connected ? $"Csatlakozva: {ssid}"
                : connecting ? $"Csatlakozás folyamatban: {ssid}"
                : $"Mentett hálózat: {ssid}";
        }
    }

    // Leállítja a folyamatban lévő (vagy aktív) csatlakozási kísérletet, ÉS
    // elfelejti a mentett SSID/jelszót az eszközön - legközelebb nem próbál
    // automatikusan csatlakozni induláskor.
    private async void OnDisconnectWifiClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;
        await BluetoothManager.WriteAsync("WIFI_FORGET");
    }

    private void HandleWifiOtaProgress(int pct)
    {
        UpdateProgressLayout.IsVisible = true;
        UpdateProgressBar.Progress = pct / 100.0;
        LblUpdateStatus.Text = $"WiFi letöltés + telepítés: {pct}%";
    }

    // ════════════════════════════════════════
    // KOMPENZÁCIÓ
    // ════════════════════════════════════════
    private void ParseLoadComp(string body)
    {
        var parts = body.Split(':');
        if (parts.Length < 2) return;
        CompNtc1Entry.Text = parts[0];
        CompNtc2Entry.Text = parts[1];
        CompPanel.IsVisible = true;
    }

    private async void OnSaveCompClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;
        float n1 = ParseFloatInvariant(CompNtc1Entry.Text, 0f);
        float n2 = ParseFloatInvariant(CompNtc2Entry.Text, 0f);
        await BluetoothManager.WriteAsync($"SET_COMP:{n1.ToString("F1", CultureInfo.InvariantCulture)}:{n2.ToString("F1", CultureInfo.InvariantCulture)}");
        await DisplayAlert("Siker", "Kompenzáció elmentve.", "OK");
    }

    // ════════════════════════════════════════
    // KIJELZŐ
    // ════════════════════════════════════════
    private void ParseLoadDisplay(string body)
    {
        var parts = body.Split(':');
        if (parts.Length < 2) return;
        if (int.TryParse(parts[0], out int brightness))
        {
            BrightnessSlider.Value = brightness;
            BrightnessLabel.Text = $"{brightness}%";
        }
        if (int.TryParse(parts[1], out int sleepIdx) && sleepIdx >= 0 && sleepIdx < SleepTimeoutPicker.Items.Count)
            SleepTimeoutPicker.SelectedIndex = sleepIdx;
        DisplayPanel.IsVisible = true;
    }

    private void OnBrightnessChanged(object sender, ValueChangedEventArgs e)
    {
        BrightnessLabel.Text = $"{(int)e.NewValue}%";
    }

    private async void OnSaveDisplayClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;
        int brightness = (int)BrightnessSlider.Value;
        int sleepIdx = SleepTimeoutPicker.SelectedIndex < 0 ? 8 : SleepTimeoutPicker.SelectedIndex;
        await BluetoothManager.WriteAsync($"SET_DISPLAY:{brightness}:{sleepIdx}");
        await DisplayAlert("Siker", "Kijelző beállítások elmentve.", "OK");
    }

    // ════════════════════════════════════════
    // DÁTUM/IDŐ
    // ════════════════════════════════════════
    private void ParseLoadDatePos(string body)
    {
        var parts = body.Split(':');
        if (parts.Length < 4) return;
        SetPickerIndexSafe(ClockPosPicker, parts[0]);
        SetPickerIndexSafe(DatePosPicker, parts[1]);
        SetPickerIndexSafe(DayNamePosPicker, parts[2]);
        SetPickerIndexSafe(TimeFormatPicker, parts[3]);
        if (parts.Length >= 5) SetPickerIndexSafe(DateFormatPicker, parts[4]);
        DateTimePanel.IsVisible = true;
    }

    private void SetPickerIndexSafe(Picker picker, string idxStr)
    {
        if (int.TryParse(idxStr, out int idx) && idx >= 0 && idx < picker.Items.Count)
            picker.SelectedIndex = idx;
    }

    private async void OnSaveDateTimeClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;
        int clockPos = Math.Max(ClockPosPicker.SelectedIndex, 0);
        int datePos = Math.Max(DatePosPicker.SelectedIndex, 0);
        int dayNamePos = Math.Max(DayNamePosPicker.SelectedIndex, 0);
        int timeFormat = Math.Max(TimeFormatPicker.SelectedIndex, 0);
        int dateFormat = Math.Max(DateFormatPicker.SelectedIndex, 0);
        await BluetoothManager.WriteAsync($"SET_DATEPOS:{clockPos}:{datePos}:{dayNamePos}:{timeFormat}:{dateFormat}");
        await DisplayAlert("Siker", "Dátum/Idő beállítások elmentve.", "OK");
    }

    // ════════════════════════════════════════
    // ESZKÖZ WIFI - keresés, hálózat-választás, csatlakozás valós idejű
    // visszajelzéssel (csatlakozik / rossz jelszó / sikertelen).
    // ════════════════════════════════════════
    private List<(string Ssid, int Rssi)> _scannedNetworks = new();
    private TaskCompletionSource<bool> _wifiScanTcs;
    private TaskCompletionSource<string> _wifiConnectTcs; // eredmény: "OK"/"WRONG_PASSWORD"/"FAILED:<ok>"
    private TaskCompletionSource<bool> _wifiStatusRefreshTcs;
    private string _pendingConnectSsid = "";

    private async void OnScanWifiClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;

        _scannedNetworks.Clear();
        WifiNetworksPicker.IsVisible = false;
        WifiNetworksPicker.ItemsSource = null;
        WifiScanIndicator.IsRunning = true;
        LblWifiStatus.Text = "WiFi állapot: hálózatok keresése...";

        _wifiScanTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            await BluetoothManager.WriteAsync("WIFI_SCAN");
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            await _wifiScanTcs.Task.WaitAsync(cts.Token);

            if (_scannedNetworks.Count == 0)
            {
                LblWifiStatus.Text = "WiFi állapot: nem található hálózat";
            }
            else
            {
                // Erősebb jel elöl
                var sorted = _scannedNetworks.OrderByDescending(n => n.Rssi).Select(n => n.Ssid).ToList();
                WifiNetworksPicker.ItemsSource = sorted;
                WifiNetworksPicker.IsVisible = true;
                LblWifiStatus.Text = $"WiFi állapot: {sorted.Count} hálózat található";
            }
        }
        catch (OperationCanceledException)
        {
            LblWifiStatus.Text = "WiFi állapot: keresés időtúllépés";
        }
        finally { WifiScanIndicator.IsRunning = false; }
    }

    private void OnWifiNetworkSelected(object sender, EventArgs e)
    {
        // Nincs extra teendő - a kiválasztott SSID a Picker.SelectedItem-ből
        // olvasódik ki csatlakozáskor. A jelszó mezőre fókuszálunk, hogy a
        // felhasználó rögtön gépelhessen.
        WifiPasswordEntry.Focus();
    }

    private async void OnSaveWifiClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;
        string ssid = WifiNetworksPicker.SelectedItem as string ?? "";
        string pass = WifiPasswordEntry.Text ?? "";
        if (string.IsNullOrWhiteSpace(ssid))
        {
            await DisplayAlert("Hiba", "Előbb keress rá a hálózatokra, és válassz egyet a listából!", "OK");
            return;
        }

        WifiStatusDot.Fill = Colors.Orange;
        LblWifiStatus.Text = $"WiFi állapot: csatlakozás ehhez: {ssid}...";
        _pendingConnectSsid = ssid;

        _wifiConnectTcs = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            await BluetoothManager.WriteAsync($"SET_WIFI:{ssid}:{pass}");

            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            string result;
            try { result = await _wifiConnectTcs.Task.WaitAsync(cts.Token); }
            catch (OperationCanceledException) { result = "FAILED:Időtúllépés"; }

            if (result == "OK")
            {
                WifiStatusDot.Fill = Colors.LightGreen;
            }
            else if (result == "WRONG_PASSWORD")
            {
                WifiStatusDot.Fill = Colors.Red;
                LblWifiStatus.Text = "WiFi állapot: HIBÁS JELSZÓ";
                await DisplayAlert("Hibás jelszó", $"A(z) \"{ssid}\" hálózathoz megadott jelszó helytelen.", "OK");
            }
            else if (result.StartsWith("FAILED:"))
            {
                WifiStatusDot.Fill = Colors.Red;
                string reason = result.Substring(7);
                LblWifiStatus.Text = $"WiFi állapot: sikertelen ({reason})";
                await DisplayAlert("Csatlakozás sikertelen", reason, "OK");
            }
        }
        finally { }
    }

    private async void OnFactoryResetClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;
        bool ok = await DisplayAlert("Gyári visszaállítás",
            "Biztos, hogy visszaállítja a gyári beállításokat?\nMinden mentett beállítás elvész!",
            "IGEN", "MÉGSE");
        if (!ok) return;

        await BluetoothManager.WriteAsync("FACTORY_RESET");
        await DisplayAlert("Gyári visszaállítás", "Az eszköz törli a beállításokat és újraindul.\nCsatlakoztasd újra pár másodperc múlva.", "OK");
        await BluetoothManager.DisconnectDeviceAsync();
        UpdateLocalUI(false, "Kapcsolat megszakadt");
    }

    // ════════════════════════════════════════
    // MÓD
    // ════════════════════════════════════════
    private bool _suppressModeChangeSend = false;

    private async void OnModeChanged(object sender, EventArgs e)
    {
        if (_suppressModeChangeSend) return;
        if (ModePicker.SelectedIndex < 0 || !BluetoothManager.IsConnected) return;
        try
        {
            await BluetoothManager.WriteAsync($"SET_MODE:{ModePicker.SelectedIndex}");
        }
        catch (Exception ex) { await DisplayAlert("Hiba", ex.Message, "OK"); }
    }

    private async void OnDisconnectClicked(object sender, EventArgs e)
    {
        if (BluetoothManager.ConnectedDevice == null) return;
        await BluetoothManager.DisconnectDeviceAsync();
        UpdateLocalUI(false, "Kapcsolat megszakadt");
        await DisplayAlert("BT", "Lecsatlakoztatva.", "OK");
    }

    // ════════════════════════════════════════
    // GITHUB ALAPÚ ONLINE FIRMWARE FRISSÍTÉS
    // Letölti a release listát a GitHubról, összeveti a jelenlegi
    // firmware-verzióval, és a legördülőben BÁRMELYIK korábbi/újabb release
    // kiválasztható - tehát nemcsak upgrade, hanem downgrade is lehetséges.
    // ════════════════════════════════════════
    private async void OnCheckReleasesClicked(object sender, EventArgs e)
    {
        try
        {
            LblUpdateAvailable.IsVisible = false;
            ReleasePicker.IsVisible = false;
            BtnGitHubUpdate.IsVisible = false;
            ScanIndicator.IsRunning = true;

            _releases = await GitHubUpdateService.GetAllReleasesAsync();
            if (_releases.Count == 0)
            {
                await DisplayAlert("Firmware", "Nem található elérhető release a GitHub repóban.", "OK");
                return;
            }

            ReleasePicker.ItemsSource = _releases.Select(r => r.DisplayLabel).ToList();

            var newest = _releases.First();
            if (!string.IsNullOrEmpty(_currentFirmwareVersion) &&
                GitHubUpdateService.CompareVersions(newest.TagName, _currentFirmwareVersion) > 0)
            {
                LblUpdateAvailable.Text = $"Új verzió elérhető: {newest.TagName}";
                LblUpdateAvailable.IsVisible = true;
                ReleasePicker.SelectedIndex = 0; // az app a legújabbat ajánlja fel elsőnek
            }
            else
            {
                LblUpdateAvailable.Text = "A jelenlegi firmware a legújabb verzió.";
                LblUpdateAvailable.TextColor = Colors.LightGreen;
                LblUpdateAvailable.IsVisible = true;
                // Ha nincs újabb, akkor is hagyjuk kiválasztani (downgrade céljából)
                int curIdx = _releases.FindIndex(r =>
                    GitHubUpdateService.CompareVersions(r.TagName, _currentFirmwareVersion) == 0);
                ReleasePicker.SelectedIndex = curIdx >= 0 ? curIdx : 0;
            }

            ReleasePicker.IsVisible = true;
            BtnGitHubUpdate.IsVisible = true;
        }
        catch (Exception ex)
        {
            await DisplayAlert("Hiba", $"Release lista lekérése sikertelen:\n{ex.Message}\n\n" +
                "Ellenőrizd, hogy be van-e állítva a GitHub repó neve a GitHubUpdateService.cs-ben.", "OK");
        }
        finally { ScanIndicator.IsRunning = false; }
    }

    private async void OnGitHubUpdateClicked(object sender, EventArgs e)
    {
        if (_isUpdating || !BluetoothManager.IsConnected) return;
        int idx = ReleasePicker.SelectedIndex;
        if (idx < 0 || idx >= _releases.Count) return;
        var release = _releases[idx];

        bool downgrade = !string.IsNullOrEmpty(_currentFirmwareVersion) &&
            GitHubUpdateService.CompareVersions(release.TagName, _currentFirmwareVersion) < 0;

        // Friss WiFi-állapot lekérdezése MOST, közvetlenül a döntés előtt -
        // ne egy esetleg percekkel korábbi (elavult) _deviceWifiConnected
        // értékre támaszkodjunk, mert az tévesen a lassú BLE-útra terelhetné
        // a frissítést, holott az eszköz épp csatlakozva van WiFi-re.
        _wifiStatusRefreshTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        await BluetoothManager.WriteAsync("GET_WIFI_STATUS");
        using (var refreshCts = new CancellationTokenSource(TimeSpan.FromSeconds(5)))
        {
            try { await _wifiStatusRefreshTcs.Task.WaitAsync(refreshCts.Token); }
            catch (OperationCanceledException) { /* nincs válasz - a legutóbb ismert állapottal folytatjuk */ }
        }

        // Ha az eszköz jelenleg csatlakozva van a saját WiFi hálózatához, a
        // GYORS utat használjuk: csak az URL-t küldjük el BLE-n, az ESP32
        // saját maga tölti le és telepíti - a telefonnak nem kell letöltenie
        // és nem kell a lassú BLE-n átküldenie a fájlt.
        if (_deviceWifiConnected)
        {
            bool okWifi = await DisplayAlert("Online frissítés (WiFi)",
                $"{(downgrade ? "VISSZAÁLLÁS" : "Frissítés")} erre a verzióra: {release.TagName}\n" +
                $"Fájl: {release.BinAssetName} ({release.BinAssetSize / 1024} KB)\nFolytatod?",
                "IGEN", "MÉGSE");
            if (!okWifi) return;

            await StartWifiOta(release.TagName, release.BinAssetUrl);
            return;
        }

        bool ok = await DisplayAlert("Online frissítés (BLE)",
            $"{(downgrade ? "VISSZAÁLLÁS" : "Frissítés")} erre a verzióra: {release.TagName}\n" +
            $"Fájl: {release.BinAssetName} ({release.BinAssetSize / 1024} KB)\nFolytatod?",
            "IGEN", "MÉGSE");
        if (!ok) return;

        byte[] fw;
        try
        {
            ScanIndicator.IsRunning = true;
            LblUpdateStatus.Text = "Letöltés a GitHubról...";
            UpdateProgressLayout.IsVisible = true;
            UpdateProgressBar.Progress = 0;

            var progress = new Progress<double>(p =>
            {
                UpdateProgressBar.Progress = p * 0.3; // a letöltés a teljes folyamat ~30%-a
                LblUpdateStatus.Text = $"Letöltés: {(int)(p * 100)}%";
            });
            fw = await GitHubUpdateService.DownloadFirmwareAsync(release, progress);
        }
        catch (Exception ex)
        {
            UpdateProgressLayout.IsVisible = false;
            await DisplayAlert("Hiba", $"Letöltés sikertelen:\n{ex.Message}", "OK");
            return;
        }
        finally { ScanIndicator.IsRunning = false; }

        await RunOtaUpload(fw, release.BinAssetName, baseProgress: 0.3);
    }

    // WiFi-alapú OTA indítása: csak az URL-t küldjük el, az ESP32 saját maga
    // tölti le és telepíti. A folyamatot az OnSettingsDataReceived-ben kapott
    // WIFI_CONNECTING / WIFI_CONNECTED / OTA_PROGRESS / OK_RESTART / ERROR
    // üzenetek vezetik végig.
    private async Task StartWifiOta(string version, string url)
    {
        _isUpdating = true;
        BtnSearch.IsEnabled = false;
        UpdateProgressLayout.IsVisible = true;
        UpdateProgressBar.Progress = 0;
        LblUpdateStatus.Text = "WiFi csatlakozás indítása...";

        // FONTOS: a firmware a tényleges letöltés/telepítés idejére teljesen
        // LEÁLLÍTJA a BLE stacket (memóriát szabadít fel a HTTPS kapcsolathoz),
        // ezért az "OK_RESTART"/"ERROR" visszajelzés SOSEM fog megérkezni BLE-n
        // - a BLE kapcsolat megszakadása maga a jel, hogy a folyamat elindult.
        // Onnantól a telefon nem tud élőben követni semmit - a kijelző mutatja
        // a letöltés/telepítés állapotát, a telefonnak újra kell csatlakoznia
        // frissítés után.
        var disconnectTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnDisconnect(bool connected) { if (!connected) disconnectTcs.TrySetResult(true); }
        BluetoothManager.OnConnectionChanged += OnDisconnect;

        try
        {
            await BluetoothManager.WriteAsync($"OTA_URL:{version}:{url}");

            LblUpdateStatus.Text = "Eszköz csatlakozik a WiFi-re...";
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
            try { await disconnectTcs.Task.WaitAsync(cts.Token); }
            catch (OperationCanceledException)
            {
                throw new Exception("Az eszköz nem kezdte el a frissítést 30 másodpercen belül.\nEllenőrizd a kijelzőn (pl. hibás WiFi jelszó).");
            }

            UpdateProgressBar.Progress = 1.0;
            LblUpdateStatus.Text = "Letöltés/telepítés folyamatban - kövesd a kijelzőn";
            await DisplayAlert("WiFi frissítés folyamatban",
                "Az eszköz a saját WiFi kapcsolatán tölti le és telepíti a firmware-t.\n\n" +
                "A telefon innentől NEM tudja élőben követni - a folyamatot a Pascal-Controller kijelzőjén kövesd. " +
                "Az eszköz a frissítés végén (sikeresen vagy hiba esetén is) automatikusan újraindul.\n\n" +
                "Ha kész, csatlakozz újra az eszközhöz.", "OK");
        }
        catch (Exception ex)
        {
            await DisplayAlert("WiFi OTA hiba", ex.Message, "OK");
        }
        finally
        {
            BluetoothManager.OnConnectionChanged -= OnDisconnect;
            _isUpdating = false;
            UpdateProgressLayout.IsVisible = false;
            BtnSearch.IsEnabled = true;
        }
    }


    // ════════════════════════════════════════
    // OTA — minden csomag WithResponse-szal megy (valódi GATT ACK
    // csomagonként, lásd BluetoothManager.OtaWriteAsync). Lassabb,
    // mint a korábbi WithoutResponse próbálkozás, de nem torlódik
    // az Android GATT queue-ja. Az ESP a végén küld OK_RESTART-ot.
    // ════════════════════════════════════════
    private async void OnUpdateFirmwareClicked(object sender, EventArgs e)
    {
        if (_isUpdating || !BluetoothManager.IsConnected) return;

        var picked = await FilePicker.Default.PickAsync(
            new PickOptions { PickerTitle = "Válassz .bin fájlt" });
        if (picked == null) return;

        bool ok = await DisplayAlert("OTA",
            $"Feltöltés: {picked.FileName}\nPartíció: Minimal SPIFFS (1.9MB APP with OTA)\nFolytatod?",
            "IGEN", "MÉGSE");
        if (!ok) return;

        byte[] fw = await File.ReadAllBytesAsync(picked.FullPath);
        await RunOtaUpload(fw, picked.FileName, baseProgress: 0);
    }

    // Közös OTA feltöltő logika - mind a helyi fájlból, mind a GitHub-ról
    // letöltött firmware-t ugyanaz a függvény küldi fel BLE-n keresztül.
    // baseProgress: 0..1 közötti eltolás a progress báron (GitHub letöltés
    // esetén a letöltés már elfoglalta a sáv egy részét).
    private async Task RunOtaUpload(byte[] fw, string fileName, double baseProgress)
    {
        _isUpdating = true;
        BtnSearch.IsEnabled = false;
        UpdateProgressLayout.IsVisible = true;
        if (baseProgress == 0) UpdateProgressBar.Progress = 0;
        LblUpdateStatus.Text = "Előkészítés...";

        BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
        BluetoothManager.OnOtaRawReceived -= OnOtaRaw;
        BluetoothManager.OnOtaRawReceived += OnOtaRaw;
        BluetoothManager.OtaMode = true;

        try
        {
            int total = fw.Length;
            int chunkSize = _otaMtu;
            Debug.WriteLine($"OTA: {total} byte, chunk: {chunkSize}");

            _readyTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            _restartTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            _otaCts = new CancellationTokenSource();

            LblUpdateStatus.Text = "Kapcsolat felépítése...";
            bool sent = await BluetoothManager.OtaWriteAsync($"UPDATE:{total}:{chunkSize}\n");
            if (!sent) throw new Exception("UPDATE parancs küldése sikertelen.\nEllenőrizd a BLE kapcsolatot.");

            LblUpdateStatus.Text = "Várakozás READY jelre...";
            using (var readyCts = new CancellationTokenSource(TimeSpan.FromSeconds(20)))
            {
                try { await _readyTcs.Task.WaitAsync(readyCts.Token); }
                catch (OperationCanceledException)
                {
                    throw new Exception(
                        "Időtúllépés: az eszköz nem küldte a READY jelet.\n\n" +
                        "• Minimal SPIFFS (1.9MB APP with OTA) partition scheme?\n" +
                        "• USB-n feltöltve az OTA-képes firmware?");
                }
            }

            Debug.WriteLine("OTA: READY – fire-and-forget küldés");
            await Task.Delay(600);

            int interChunkDelay = 50;
            int sentBytes = 0;
            int chunkIndex = 0;

            for (int offset = 0; offset < total; offset += chunkSize)
            {
                int csz = Math.Min(chunkSize, total - offset);
                byte[] chunk = new byte[csz];
                Array.Copy(fw, offset, chunk, 0, csz);

                if (_otaCts.Token.IsCancellationRequested)
                    throw new Exception("OTA megszakítva: az eszköz hibát jelzett.");

                bool writeOk = await BluetoothManager.OtaWriteAsync(chunk);

                if (!writeOk)
                {
                    interChunkDelay = Math.Min(interChunkDelay + 50, 400);
                    Debug.WriteLine($"OTA write FAILED @ {offset / 1024}KB → retry {interChunkDelay}ms");
                    await Task.Delay(interChunkDelay);
                    writeOk = await BluetoothManager.OtaWriteAsync(chunk);
                    if (!writeOk)
                    {
                        await Task.Delay(interChunkDelay);
                        writeOk = await BluetoothManager.OtaWriteAsync(chunk);
                    }
                    if (!writeOk)
                        throw new Exception($"BLE írás sikertelen {offset / 1024}KB-nál.\nPróbáld újra.");
                }

                sentBytes += csz;
                chunkIndex++;

                await Task.Delay(interChunkDelay);

                if (writeOk && interChunkDelay > 50)
                    interChunkDelay = Math.Max(interChunkDelay - 2, 50);

                if (chunkIndex % 20 == 0 || sentBytes >= total)
                {
                    double pct = Math.Min(1.0, (double)sentBytes / total);
                    double shownPct = baseProgress + pct * (1 - baseProgress);
                    int sb = sentBytes; int d2 = interChunkDelay;
                    MainThread.BeginInvokeOnMainThread(() =>
                    {
                        UpdateProgressBar.Progress = shownPct;
                        LblUpdateStatus.Text =
                            $"{(int)(pct * 100)}%  ({sb / 1024} / {total / 1024} KB)  [{d2}ms]";
                    });
                }
            }

            MainThread.BeginInvokeOnMainThread(() =>
            {
                UpdateProgressBar.Progress = 1.0;
                LblUpdateStatus.Text = "Flash verifikáció, az eszköz újraindul...";
            });

            using (var rCts = new CancellationTokenSource(TimeSpan.FromSeconds(30)))
            {
                try { await _restartTcs.Task.WaitAsync(rCts.Token); }
                catch (OperationCanceledException)
                { Debug.WriteLine("OK_RESTART timeout – lehet hogy már újraindult"); }
            }

            await Task.Delay(4000);
            if (BluetoothManager.IsConnected)
                await BluetoothManager.DisconnectDeviceAsync();

            await DisplayAlert("Siker", "Firmware sikeresen frissítve!\nAz eszköz újraindult.", "OK");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"OTA HIBA: {ex.Message}");
            await DisplayAlert("OTA Hiba", ex.Message, "OK");
        }
        finally
        {
            _isUpdating = false;
            _otaCts?.Cancel();
            BluetoothManager.OtaMode = false;
            BluetoothManager.OnOtaRawReceived -= OnOtaRaw;
            BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
            BluetoothManager.OnDataReceived += OnSettingsDataReceived;
            MainThread.BeginInvokeOnMainThread(() =>
            {
                UpdateProgressLayout.IsVisible = false;
                BtnSearch.IsEnabled = true;
            });
        }
    }

    private void OnOtaRaw(string message)
    {
        if (string.IsNullOrWhiteSpace(message)) return;
        var lines = message.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
        foreach (var raw in lines)
        {
            var m = raw.Trim();
            if (string.IsNullOrEmpty(m)) continue;
            Debug.WriteLine($"[OTA RX]: '{m}'");
            switch (m)
            {
                case "READY":
                    _readyTcs?.TrySetResult(true);
                    break;
                case "OK_RESTART":
                    _restartTcs?.TrySetResult(true);
                    Debug.WriteLine("OTA: az eszköz újraindul!");
                    break;
                case "ERROR":
                    Debug.WriteLine("OTA: hiba - OTA megszakítva");
                    _otaCts?.Cancel();
                    break;
                default:
                    break;
            }
        }
    }

    private void LoadConnectedDevice()
    {
        bool c = BluetoothManager.IsConnected;
        string n = BluetoothManager.GetSavedDeviceName() ?? "Nincs eszköz";
        UpdateLocalUI(c, n);
    }

    private void UpdateLocalUI(bool connected, string name)
    {
        ConnectedDevicePanel.IsVisible = true;
        LblConnectedDevice.Text = connected ? $"Csatlakozva: {name}" : "Nincs csatlakozva";
        BtnDisconnect.IsVisible = connected;
        ModeSelectionPanel.IsVisible = connected;
        WifiPanel.IsVisible = connected;
        GitHubOtaPanel.IsVisible = connected;
        FactoryResetPanel.IsVisible = connected;
        // Eszközkeresés csak akkor látszik, ha NINCS aktív kapcsolat - ha már
        // csatlakoztál, nincs értelme tovább mutatni a keresés/lista UI-t.
        DeviceSearchSection.IsVisible = !connected;
        // Comp/Display/DateTime panelek csak akkor válnak láthatóvá, ha a
        // megfelelő LOAD_* válasz megérkezett (lásd OnSettingsDataReceived) -
        // lecsatlakozáskor viszont mindenképp el kell tüntetni őket.
        if (!connected)
        {
            CompPanel.IsVisible = false;
            DisplayPanel.IsVisible = false;
            DateTimePanel.IsVisible = false;
            CollapseAllSections();
        }
    }

    // Minden lenyitható szekció visszazárása (pl. lecsatlakozáskor, hogy
    // legközelebbi csatlakozáskor újra tiszta, csukott állapotból induljon).
    private void CollapseAllSections()
    {
        CompDetailsPanel.IsVisible = false; CompExpandIcon.Text = "▼";
        DisplayDetailsPanel.IsVisible = false; DisplayExpandIcon.Text = "▼";
        DateTimeDetailsPanel.IsVisible = false; DateTimeExpandIcon.Text = "▼";
        WifiDetailsPanel.IsVisible = false; WifiExpandIcon.Text = "▼";
        FirmwareDetailsPanel.IsVisible = false; FirmwareExpandIcon.Text = "▼";
        FactoryResetDetailsPanel.IsVisible = false; FactoryResetExpandIcon.Text = "▼";
    }

    // ══════════════ LENYITHATÓ SZEKCIÓ-FEJLÉCEK ══════════════
    private void ToggleSection(VerticalStackLayout panel, Label icon)
    {
        panel.IsVisible = !panel.IsVisible;
        icon.Text = panel.IsVisible ? "▲" : "▼";
    }
    private void OnCompHeaderTapped(object s, EventArgs e) => ToggleSection(CompDetailsPanel, CompExpandIcon);
    private void OnDisplayHeaderTapped(object s, EventArgs e) => ToggleSection(DisplayDetailsPanel, DisplayExpandIcon);
    private void OnDateTimeHeaderTapped(object s, EventArgs e) => ToggleSection(DateTimeDetailsPanel, DateTimeExpandIcon);
    private void OnWifiHeaderTapped(object s, EventArgs e) => ToggleSection(WifiDetailsPanel, WifiExpandIcon);
    private void OnFirmwareHeaderTapped(object s, EventArgs e) => ToggleSection(FirmwareDetailsPanel, FirmwareExpandIcon);
    private void OnFactoryResetHeaderTapped(object s, EventArgs e) => ToggleSection(FactoryResetDetailsPanel, FactoryResetExpandIcon);

    // Az eszköz dátumát/idejét MINDIG automatikusan szinkronizáljuk a
    // telefon aktuális idejével - kézi beállítás nincs és nem is szükséges.
    private async Task SyncTimeAsync()
    {
        var n = DateTime.Now;
        await BluetoothManager.WriteAsync($"SET_DATE:{n.Year}:{n.Month}:{n.Day}");
        await Task.Delay(120);
        await BluetoothManager.WriteAsync($"TIME:{n.Hour:D2}:{n.Minute:D2}:{n.Second:D2}");
    }

    private async Task<PermissionStatus> CheckBTPerm()
    {
        var s = await Permissions.CheckStatusAsync<Permissions.Bluetooth>();
        if (s != PermissionStatus.Granted)
            s = await Permissions.RequestAsync<Permissions.Bluetooth>();
        return s;
    }

    private bool CheckBLE()
    {
        if (BluetoothManager.IsConnected) return true;
        _ = DisplayAlert("Hiba", "Nincs BLE kapcsolat!", "OK");
        return false;
    }

    private static float ParseFloatInvariant(string s, float fallback)
        => float.TryParse(s?.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out float v) ? v : fallback;

    // ════════════════════════════════════════
    // SÉMA MENTÉS / BETÖLTÉS / MEGOSZTÁS
    // A "séma" fájl egyszerű szöveges fájl, soronként a controller nyers
    // BLE válaszsoraival (STATE:, LOAD_COMP:, LOAD_DISPLAY:, LOAD_DATEPOS:,
    // LOAD_FTIMER:, LOAD_RTIMER:, LOAD_THERM:, LOAD_LOGIC:, LOAD_RHOLD:) -
    // betöltéskor ezeket alakítjuk vissza a megfelelő SET_* parancsokká.
    // ════════════════════════════════════════
    private const string SchemeFileExt = ".pcscheme";
    private List<string> _capturedSchemeLines;
    private List<string> _schemeFilePaths = new(); // a SchemePicker elemeivel azonos sorrendben

    private string SchemesFolder()
    {
        string folder = Path.Combine(FileSystem.Current.AppDataDirectory, "Schemes");
        Directory.CreateDirectory(folder);
        return folder;
    }

    private async void OnSaveSchemeClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;

        string name = await DisplayPromptAsync("Séma mentése", "Add meg a séma nevét:", "Mentés", "Mégse", placeholder: "pl. Nyári beállítás", maxLength: 60);
        if (string.IsNullOrWhiteSpace(name)) return;

        LblSchemeStatus.Text = "Beállítások lekérdezése az eszközről...";
        _capturedSchemeLines = new List<string>();

        BluetoothManager.OnDataReceived -= OnSchemeCaptureData;
        BluetoothManager.OnDataReceived += OnSchemeCaptureData;

        try
        {
            // Minden beállítás-csoport lekérdezése - a válaszokat
            // OnSchemeCaptureData gyűjti a _capturedSchemeLines listába.
            await BluetoothManager.WriteAsync("GET_STATE");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_COMP");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_DISPLAY");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_DATEPOS");
            await Task.Delay(150);
            await BluetoothManager.WriteAsync("GET_ALL"); // ventilátor/relé görbék, logika, időzítők, kapuzás mind

            // Az eszköz több tucat sort küld vissza BLE notify-n - erre időt
            // kell hagyni, mielőtt lezárjuk a mentést.
            await Task.Delay(2500);

            if (_capturedSchemeLines.Count == 0)
            {
                LblSchemeStatus.Text = "Hiba: nem érkezett adat az eszköztől.";
                return;
            }

            string safeName = new string(name.Where(c => !Path.GetInvalidFileNameChars().Contains(c)).ToArray());
            if (string.IsNullOrWhiteSpace(safeName)) safeName = "sema";
            string fileName = $"{safeName}_{DateTime.Now:yyyyMMdd_HHmmss}{SchemeFileExt}";
            string path = Path.Combine(SchemesFolder(), fileName);

            var content = new List<string>
            {
                "# Pascal-Controller mentett séma",
                $"# Nev: {name}",
                $"# Mentve: {DateTime.Now:yyyy.MM.dd HH:mm:ss}"
            };
            content.AddRange(_capturedSchemeLines.Distinct());
            await File.WriteAllLinesAsync(path, content);

            LblSchemeStatus.Text = $"Elmentve: \"{name}\"";
            RefreshSchemePicker(selectPath: path);
        }
        finally
        {
            BluetoothManager.OnDataReceived -= OnSchemeCaptureData;
        }
    }

    private void OnSchemeCaptureData(string data)
    {
        if (string.IsNullOrWhiteSpace(data)) return;
        var d = data.Trim();
        if (d.StartsWith("STATE:") || d.StartsWith("LOAD_"))
            _capturedSchemeLines?.Add(d);
    }

    private async void OnLoadSchemeClicked(object sender, EventArgs e)
    {
        if (!CheckBLE()) return;

        int idx = SchemePicker.SelectedIndex;
        if (idx < 0 || idx >= _schemeFilePaths.Count)
        {
            await DisplayAlert("Séma betöltése", "Előbb válassz egy mentett sémát a listából.", "OK");
            return;
        }
        string path = _schemeFilePaths[idx];
        string choice = SchemePicker.Items[idx];

        bool confirm = await DisplayAlert("Séma betöltése",
            $"Betölti ezt: \"{choice}\"?\nFelülírja a jelenlegi beállításokat az eszközön.",
            "IGEN", "MÉGSE");
        if (!confirm) return;

        var lines = await File.ReadAllLinesAsync(path);
        int sent = 0;
        LblSchemeStatus.Text = "Betöltés folyamatban...";

        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("#")) continue;
            string cmd = LoadLineToSetCommand(line);
            if (cmd == null) continue;
            await BluetoothManager.WriteAsync(cmd);
            sent++;
            await Task.Delay(130); // ne torlódjon a BLE queue
        }

        LblSchemeStatus.Text = $"Betöltve: \"{choice}\"";

        // Frissítsük a felületet is a most betöltött állapottal - a mód
        // mellett a Kompenzáció/Kijelző/Dátum-Idő/WiFi panelek is új
        // adatot kapjanak, ne csak lapváltáskor frissüljenek.
        //
        // Védelemből újra bekötjük a feliratkozást (-= majd +=), hogy
        // biztosan aktív legyen, még akkor is, ha valami korábban
        // véletlenül leiratkoztatta volna - és egy kicsit hosszabb
        // szünetet hagyunk a hosszú SET-parancssorozat után, hogy a BLE
        // ne torlódjon össze a lekérdezésekkel.
        BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
        BluetoothManager.OnDataReceived += OnSettingsDataReceived;

        await Task.Delay(500);
        await BluetoothManager.WriteAsync("GET_STATE");
        await Task.Delay(250);
        await BluetoothManager.WriteAsync("GET_COMP");
        await Task.Delay(250);
        await BluetoothManager.WriteAsync("GET_DISPLAY");
        await Task.Delay(250);
        await BluetoothManager.WriteAsync("GET_DATEPOS");
        await Task.Delay(250);
        await BluetoothManager.WriteAsync("GET_WIFI_STATUS");
    }

    private async void OnDeleteSchemeClicked(object sender, EventArgs e)
    {
        int idx = SchemePicker.SelectedIndex;
        if (idx < 0 || idx >= _schemeFilePaths.Count)
        {
            await DisplayAlert("Séma törlése", "Előbb válassz egy mentett sémát a listából.", "OK");
            return;
        }
        string path = _schemeFilePaths[idx];
        string choice = SchemePicker.Items[idx];

        bool confirm = await DisplayAlert("Séma törlése",
            $"Biztos, hogy törlöd ezt a sémát: \"{choice}\"?\nEz nem vonható vissza.",
            "TÖRLÉS", "MÉGSE");
        if (!confirm) return;

        try
        {
            File.Delete(path);
            LblSchemeStatus.Text = $"Törölve: \"{choice}\"";
        }
        catch (Exception ex)
        {
            await DisplayAlert("Hiba", $"Törlés sikertelen:\n{ex.Message}", "OK");
        }

        RefreshSchemePicker();
    }

    private async void OnShareSchemeClicked(object sender, EventArgs e)
    {
        int idx = SchemePicker.SelectedIndex;
        if (idx < 0 || idx >= _schemeFilePaths.Count)
        {
            await DisplayAlert("Megosztás", "Előbb válassz egy mentett sémát a listából.", "OK");
            return;
        }

        await Share.Default.RequestAsync(new ShareFileRequest
        {
            Title = "Pascal-Controller séma megosztása",
            File = new ShareFile(_schemeFilePaths[idx])
        });
    }

    private List<string> GetSavedSchemeFiles()
    {
        string folder = SchemesFolder();
        return Directory.GetFiles(folder, "*" + SchemeFileExt)
            .OrderByDescending(f => File.GetLastWriteTime(f))
            .ToList();
    }

    // Újratölti a SchemePicker listáját a mentett sémákkal (dátum szerint,
    // legújabb elöl). selectPath megadása esetén azt jelöli ki kiválasztva.
    private void RefreshSchemePicker(string selectPath = null)
    {
        _schemeFilePaths = GetSavedSchemeFiles();
        SchemePicker.ItemsSource = _schemeFilePaths.Select(SchemeDisplayName).ToList();
        if (selectPath != null)
        {
            int idx = _schemeFilePaths.IndexOf(selectPath);
            if (idx >= 0) SchemePicker.SelectedIndex = idx;
        }
    }

    // A fájlnévből ("Nyari beallitas_20260822_143000.pcscheme") egy olvasható
    // címkét épít ("Nyari beallitas - 2026.08.22 14:30").
    private string SchemeDisplayName(string path)
    {
        string fn = Path.GetFileNameWithoutExtension(path);
        try
        {
            // formátum: <nev>_yyyyMMdd_HHmmss
            var parts = fn.Split('_');
            if (parts.Length >= 3)
            {
                string datePart = parts[parts.Length - 2];
                string timePart = parts[parts.Length - 1];
                string namePart = string.Join("_", parts.Take(parts.Length - 2));
                if (DateTime.TryParseExact(datePart + timePart, "yyyyMMddHHmmss", CultureInfo.InvariantCulture, DateTimeStyles.None, out var dt))
                    return $"{namePart} - {dt:yyyy.MM.dd HH:mm}";
            }
        }
        catch { }
        return fn;
    }

    // LOAD_* / STATE: válaszsor -> a megfelelő SET_* parancs. A legtöbb
    // esetben a törzs (a prefix utáni rész) egy az egyben megegyezik - a
    // ventilátor/relé IDŐZÍTŐK (FTIMER/RTIMER) kivételt jelentenek, mert a
    // LOAD válasz vesszőzött-lapos, a SET parancs viszont "|"-vel elválasztott
    // slot-csoportokat vár - ezt a ReformatTimer alakítja vissza.
    private static string LoadLineToSetCommand(string line)
    {
        if (line.StartsWith("STATE:")) return "SET_MODE:" + line.Substring(6);
        if (line.StartsWith("LOAD_COMP:")) return "SET_COMP:" + line.Substring(10);
        if (line.StartsWith("LOAD_DISPLAY:")) return "SET_DISPLAY:" + line.Substring(13);
        if (line.StartsWith("LOAD_DATEPOS:")) return "SET_DATEPOS:" + line.Substring(13);
        if (line.StartsWith("LOAD_THERM:")) return "SET_THERM:" + line.Substring(11);
        if (line.StartsWith("LOAD_LOGIC:")) return "SET_LOGIC:" + line.Substring(11);
        if (line.StartsWith("LOAD_RHOLD:")) return "SET_RHOLD:" + line.Substring(11);
        if (line.StartsWith("LOAD_FTIMER:")) return ReformatTimer("SET_FTIMER:", line.Substring(12));
        if (line.StartsWith("LOAD_RTIMER:")) return ReformatTimer("SET_RTIMER:", line.Substring(12));
        return null;
    }

    // LOAD formátum (lapos): <target>:<days>:<h1>:<m1>:<v1>:<h2>:<m2>:<v2>...
    // SET formátum (pipe-elválasztott hármasok): <target>:<days>:<h1>:<m1>:<v1>|<h2>:<m2>:<v2>|...
    private static string ReformatTimer(string setPrefix, string body)
    {
        var parts = body.Split(':');
        if (parts.Length < 2) return null;
        string target = parts[0];
        string days = parts[1];
        var slotGroups = new List<string>();
        for (int i = 2; i + 2 < parts.Length; i += 3)
            slotGroups.Add($"{parts[i]}:{parts[i + 1]}:{parts[i + 2]}");
        return $"{setPrefix}{target}:{days}:{string.Join("|", slotGroups)}";
    }
}
