using Plugin.BLE;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Text;

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

    public Settings()
    {
        InitializeComponent();
        _ble = CrossBluetoothLE.Current;
        _adapter = CrossBluetoothLE.Current.Adapter;
        DevicesListView.ItemsSource = _deviceList;
        BluetoothManager.Initialize(_adapter);
        LoadConnectedDevice();
    }

    private void Adapter_DeviceDiscovered(object sender,
        Plugin.BLE.Abstractions.EventArgs.DeviceEventArgs e)
    {
        if (!_deviceList.Any(d => d.Id == e.Device.Id) && !string.IsNullOrEmpty(e.Device.Name))
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

    private async void OnDeviceSelected(object sender, SelectionChangedEventArgs e)
    {
        if (e.CurrentSelection.FirstOrDefault() is not IDevice device) return;
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
            BluetoothManager.NotifyConnectionSuccess();
            await SyncTimeAsync();
            await Task.Delay(200);
            await BluetoothManager.WriteAsync("GET_STATE");
            await DisplayAlert("Siker", $"Csatlakozva: {device.Name}", "OK");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Csatlakozás: {ex.Message}");
            await DisplayAlert("Hiba", "Csatlakozás sikertelen!", "OK");
        }
        finally
        {
            ScanIndicator.IsRunning = false;
            DevicesListView.SelectedItem = null;
        }
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        LoadConnectedDevice();
        if (BluetoothManager.IsConnected)
        {
            BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
            BluetoothManager.OnDataReceived += OnSettingsDataReceived;
            await BluetoothManager.WriteAsync("GET_STATE");
        }
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
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
                ModePicker.SelectedIndex = m;
                ModeSelectionPanel.IsVisible = true;
            }
            else if (d.StartsWith("VER:"))
                LblFirmwareVersion.Text = d.Substring(4);
        });
    }

    private async void OnSetModeClicked(object sender, EventArgs e)
    {
        if (ModePicker.SelectedIndex < 0) return;
        try
        {
            await BluetoothManager.WriteAsync($"SET_MODE:{ModePicker.SelectedIndex}");
            await Task.Delay(300);
            await BluetoothManager.WriteAsync("GET_STATE");
            await DisplayAlert("Siker", "Üzemmód elküldve!", "OK");
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

        _isUpdating = true;
        BtnSearch.IsEnabled = false;
        UpdateProgressLayout.IsVisible = true;
        UpdateProgressBar.Progress = 0;
        LblUpdateStatus.Text = "Előkészítés...";

        BluetoothManager.OnDataReceived -= OnSettingsDataReceived;
        BluetoothManager.OnOtaRawReceived -= OnOtaRaw;
        BluetoothManager.OnOtaRawReceived += OnOtaRaw;
        BluetoothManager.OtaMode = true;

        try
        {
            byte[] fw = await File.ReadAllBytesAsync(picked.FullPath);
            int total = fw.Length;
            int chunkSize = _otaMtu;
            Debug.WriteLine($"OTA: {total} byte, chunk: {chunkSize}");

            _readyTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            _restartTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            _otaCts = new CancellationTokenSource();

            // UPDATE parancs
            LblUpdateStatus.Text = "Kapcsolat felépítése...";
            // UPDATE parancs küldése
            bool sent = await BluetoothManager.OtaWriteAsync($"UPDATE:{total}:{chunkSize}\n");
            if (!sent) throw new Exception("UPDATE parancs küldése sikertelen.\nEllenőrizd a BLE kapcsolatot.");

            LblUpdateStatus.Text = "Várakozás READY jelre...";
            using (var readyCts = new CancellationTokenSource(TimeSpan.FromSeconds(20)))
            {
                try { await _readyTcs.Task.WaitAsync(readyCts.Token); }
                catch (OperationCanceledException)
                {
                    throw new Exception(
                        "Időtúllépés: az ESP nem küldte a READY jelet.\n\n" +
                        "• Minimal SPIFFS (1.9MB APP with OTA) partition scheme?\n" +
                        "• USB-n feltöltve az OTA-képes firmware?");
                }
            }

            Debug.WriteLine("OTA: READY – fire-and-forget küldés");
            // GATT stack stabilizálódás: az UPDATE parancs GATT ACK-ja után
            // az Android stack még ~500ms-ig foglalt maradhat
            await Task.Delay(600);

            // Adaptív delay: ha "prior command" hibát kap, növeli az időközt
            int interChunkDelay = 50; // Alap delay – adaptívan növekszik ha kell
            int sentBytes = 0;
            int chunkIndex = 0;

            for (int offset = 0; offset < total; offset += chunkSize)
            {
                int csz = Math.Min(chunkSize, total - offset);
                byte[] chunk = new byte[csz];
                Array.Copy(fw, offset, chunk, 0, csz);

                if (_otaCts.Token.IsCancellationRequested)
                    throw new Exception("OTA megszakítva: ESP hibát jelzett.");

                // FONTOS: NE retry-oljunk ha az írás sikertelen!
                // Ha a chunk részben beíródott az ESP flash-be és újraküldjük,
                // az offset elcsúszik → "invalid segment length" corrupted image.
                // Ha sikertelen, növeljük a delay-t és folytatjuk (az ESP
                // a WithoutResponse miatt nem tudja hogy mi jött meg).
                bool writeOk = await BluetoothManager.OtaWriteAsync(chunk);

                if (!writeOk)
                {
                    // Chunk nem ment át – újra kell küldeni
                    // (ha kihagyjuk, MD5 mismatch lesz az Update.end()-nél)
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

                // Chunk-ok közötti várakozás – ez adja az adaptív tempót
                await Task.Delay(interChunkDelay);

                // Ha pár egymás utáni chunk sikeres volt, csökkentjük kicsit a delay-t
                if (writeOk && interChunkDelay > 50)
                    interChunkDelay = Math.Max(interChunkDelay - 2, 50);

                // UI frissítés minden 20. csomagnál
                if (chunkIndex % 20 == 0 || sentBytes >= total)
                {
                    double pct = Math.Min(1.0, (double)sentBytes / total);
                    int sb = sentBytes; int d2 = interChunkDelay;
                    MainThread.BeginInvokeOnMainThread(() =>
                    {
                        UpdateProgressBar.Progress = pct;
                        LblUpdateStatus.Text =
                            $"{(int)(pct * 100)}%  ({sb / 1024} / {total / 1024} KB)  [{d2}ms]";
                    });
                }
            }

            // Várakozás OK_RESTART-ra
            MainThread.BeginInvokeOnMainThread(() =>
            {
                UpdateProgressBar.Progress = 1.0;
                LblUpdateStatus.Text = "Flash verifikáció, ESP újraindul...";
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

            await DisplayAlert("Siker", "Firmware sikeresen frissítve!\nAz ESP újraindult.", "OK");
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
                    Debug.WriteLine("OTA: ESP újraindul!");
                    break;
                case "ERROR":
                    Debug.WriteLine("OTA: ESP ERROR – OTA megszakítva");
                    _otaCts?.Cancel();
                    break;
                default:
                    // OK és minden más: figyelmen kívül (fire-and-forget módban
                    // az ESP küldhet OK-kat de az app nem vár rájuk)
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
        FirmwareUpdatePanel.IsVisible = connected;
    }

    private async Task SyncTimeAsync()
    {
        var n = DateTime.Now;
        await BluetoothManager.WriteAsync($"TIME:{n.Hour:D2}:{n.Minute:D2}:{n.Second:D2}");
    }

    private async Task<PermissionStatus> CheckBTPerm()
    {
        var s = await Permissions.CheckStatusAsync<Permissions.Bluetooth>();
        if (s != PermissionStatus.Granted)
            s = await Permissions.RequestAsync<Permissions.Bluetooth>();
        return s;
    }
}