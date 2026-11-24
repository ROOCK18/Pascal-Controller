using Microsoft.Maui.Storage;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace Controll;

// Egy mentett eszköz: azonosító, a BLE neve és a felhasználó által megadott hely (pl. "Terrárium").
public class SavedDevice
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Location { get; set; } = "";
}

public static class BluetoothManager
{
    public static IAdapter        Adapter             { get; set; }
    public static IDevice         ConnectedDevice     { get; set; }
    public static ICharacteristic WriteCharacteristic { get; set; }
    public static ICharacteristic ReadCharacteristic  { get; set; }

    public static event Action<bool>   OnConnectionChanged;
    public static event Action<string> OnDataReceived;
    public static event Action<string> OnOtaRawReceived;
    public static bool OtaMode { get; set; } = false;

    // ── Automatikus újracsatlakozás ──
    // A mentett eszköz (BTDeviceId) megmarad, ha a kapcsolat váratlanul megszakad.
    // Ilyenkor (és az app indulásakor / előtérbe kerülésekor) a háttérben újra próbál
    // csatlakozni; az Android "autoConnect" módjában ez akkor sikerül, amikor az eszköz
    // hatótávolságba kerül. Csak a kézi "Lecsatlakozás" felejti el az eszközt.
    // Kézi lecsatlakozás után az automatikus csatlakozás kikapcsolva marad (az app újraindítása után is),
    // amíg a felhasználó kézzel újra nem csatlakozik.
    private static bool _autoReconnectEnabled = !Preferences.Get("BTAutoOff", false);
    private static bool _reconnecting;
    private static CancellationTokenSource _reconnectCts;
    private static readonly object _reconnectLock = new();

    public static bool IsReconnecting => _reconnecting;
    // Igaz, ha a legutóbbi csatlakozást az automatikus újracsatlakozás hozta létre
    // (a Settings oldal ilyenkor frissíti a kijelzett adatokat).
    public static bool LastConnectWasAuto { get; private set; }

    public static bool IsConnected =>
        ConnectedDevice != null &&
        Adapter?.ConnectedDevices.Any(d => d.Id == ConnectedDevice.Id) == true;

    public static void Initialize(IAdapter adapter)
    {
        if (Adapter != null) return;
        Adapter = adapter;
        Adapter.DeviceDisconnected   += (s, e) => CheckAndNotify(e.Device);
        Adapter.DeviceConnectionLost += (s, e) => CheckAndNotify(e.Device);
    }

    private static void CheckAndNotify(IDevice device)
    {
        if (ConnectedDevice != null && device.Id == ConnectedDevice.Id)
            HandleDisconnect();
    }

    private static void HandleDisconnect()
    {
        ConnectedDevice     = null;
        WriteCharacteristic = null;
        ReadCharacteristic  = null;
        OtaMode             = false;
        // A mentett eszköz NEM törlődik: váratlan szétkapcsolás után újra tudjunk csatlakozni.
        StartAutoReconnect();
        MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(false));
    }

    // Egy mentett eszköznél magától csatlakozik. Több mentett eszköznél NEM csatlakozik némán
    // (mert eltérő lehet a beállításuk): ilyenkor, ha allowPicker igaz (app indulás), megkérdezi
    // a felhasználót, hogy a hatótávban lévő eszközök közül melyikre csatlakozzon. Ha a kapcsolat
    // az imént használt eszközzel szakad meg, ugyanahhoz újra csatlakozik.
    public static void StartAutoReconnect(bool allowPicker = false)
    {
        if (Adapter == null || IsConnected || !_autoReconnectEnabled) return;
        var saved = GetSavedDevices();
        if (saved.Count == 0) return;
        string targetId;
        if (saved.Count == 1) targetId = saved[0].Id;
        else if (!string.IsNullOrEmpty(_sessionLastId)) targetId = _sessionLastId;
        else
        {
            if (allowPicker) _ = Task.Run(async () => { await Task.Delay(1500); await ShowDevicePickerAsync(); });
            return;
        }
        if (!Guid.TryParse(targetId, out var id)) return;
        CancellationToken ct;
        lock (_reconnectLock)
        {
            if (_reconnecting) return;
            _reconnecting = true;
            _reconnectCts = new CancellationTokenSource();
            ct = _reconnectCts.Token;
        }
        _ = Task.Run(() => ReconnectLoopAsync(id, ct));
    }

    private static void CancelReconnect()
    {
        lock (_reconnectLock) { _reconnectCts?.Cancel(); }
    }

    private static async Task ReconnectLoopAsync(Guid id, CancellationToken ct)
    {
        int attempt = 0;
        try
        {
            while (!ct.IsCancellationRequested && !IsConnected && _autoReconnectEnabled)
            {
                try
                {
                    var perm = await MainThread.InvokeOnMainThreadAsync(
                        async () => await Permissions.CheckStatusAsync<Permissions.Bluetooth>());
                    if (perm == PermissionStatus.Granted && Plugin.BLE.CrossBluetoothLE.Current.IsOn)
                    {
                        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
                        linked.CancelAfter(TimeSpan.FromSeconds(25));
                        var dev = await Adapter.ConnectToKnownDeviceAsync(
                            id, new ConnectParameters(autoConnect: true, forceBleTransport: true), linked.Token);
                        if (dev != null && await SetupConnectionAsync(dev, auto: true)) return;
                    }
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested) { return; }
                catch (Exception ex) { Debug.WriteLine($"Újracsatlakozás: {ex.Message}"); }

                attempt++;
                await Task.Delay(TimeSpan.FromSeconds(Math.Min(2 + attempt, 10)), ct);
            }
        }
        catch (OperationCanceledException) { }
        finally { lock (_reconnectLock) { _reconnecting = false; } }
    }

    // Az automatikus csatlakozás után ugyanazt a felderítést végzi, mint a kézi csatlakozás
    // (karakterisztikák, MTU, értesítések), majd szinkronizálja az időt.
    private static async Task<bool> SetupConnectionAsync(IDevice device, bool auto)
    {
        try
        {
            ConnectedDevice = device;
            WriteCharacteristic = null;
            ReadCharacteristic = null;

            var services = await device.GetServicesAsync();
            foreach (var svc in services)
            {
                var chars = await svc.GetCharacteristicsAsync();
                foreach (var ch in chars)
                {
                    if (ch.CanWrite) WriteCharacteristic = ch;
                    if (ch.CanUpdate || ch.CanRead)
                    {
                        ReadCharacteristic = ch;
                        await SubscribeToNotifications();
                    }
                }
            }
            if (WriteCharacteristic == null) throw new InvalidOperationException("Nincs írható karakterisztika");

            await RequestMtuAsync(517);
            SaveConnectedDevice(device);
            LastConnectWasAuto = auto;
            NotifyConnectionSuccess();

            var n = DateTime.Now;
            await WriteAsync($"SET_DATE:{n.Year}:{n.Month}:{n.Day}");
            await Task.Delay(120);
            await WriteAsync($"TIME:{n.Hour:D2}:{n.Minute:D2}:{n.Second:D2}");
            await Task.Delay(150);
            await WriteAsync("GET_STATE");
            return true;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Csatlakozás beállítása sikertelen: {ex.Message}");
            try { await Adapter.DisconnectDeviceAsync(device); } catch { }
            ConnectedDevice = null; WriteCharacteristic = null; ReadCharacteristic = null;
            return false;
        }
    }

    public static void NotifyConnectionSuccess()
        => MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(true));

    // ── Normál írás ──
    private static readonly SemaphoreSlim _writeLock = new(1, 1);
    private static CancellationTokenSource _cts = new();

    public static async Task WriteAsync(string message)
    {
        if (WriteCharacteristic == null) return;
        _cts.Cancel();
        _cts = new CancellationTokenSource();
        var token = _cts.Token;
        await _writeLock.WaitAsync();
        try
        {
            if (token.IsCancellationRequested) return;
            WriteCharacteristic.WriteType = CharacteristicWriteType.WithResponse;
            await WriteCharacteristic.WriteAsync(Encoding.UTF8.GetBytes(message));
            await Task.Delay(15, token);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Debug.WriteLine($"WriteAsync: {ex.Message}"); }
        finally { _writeLock.Release(); }
    }

    // ── OTA írás ──
    // EGYSZERŰ, timeout-os egyetlen kísérlet. Retry-t a hívó (Settings) végzi.
    // Timeout: ha az Android GATT callback nem jön vissza 2mp-en belül,
    // cancellálunk - ez jelzi hogy a GATT stack el is dobta az írást.
    public static async Task<bool> OtaWriteAsync(byte[] data)
    {
        if (WriteCharacteristic == null) return false;
        try
        {
            // KIZÁRÓLAG WithResponse: a WithoutResponse próbálkozást eltávolítottuk,
            // mert az Android BLE stack "prior command is not finished" hibával
            // dobta el az egymást gyorsan követő írásokat - a Plugin.BLE ugyanis
            // WithoutResponse módban nem minden esetben várja meg ténylegesen a
            // natív írás helyi lezárását, így a hívó (Settings) korábban futott
            // tovább, mint hogy a rádió ténylegesen elküldte/nyugtázta volna az
            // előző csomagot -> csomagvesztés/torlódás/OTA hiba. WithResponse-nál
            // ez strukturálisan kizárt, mert a GATT válasz megérkezéséig nem térünk
            // vissza, tehát a következő csomag csak utána indulhat.
            WriteCharacteristic.WriteType = CharacteristicWriteType.WithResponse;
            using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(2000));
            await WriteCharacteristic.WriteAsync(data).WaitAsync(cts.Token);
            return true;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"OtaWrite FAILED: {ex.Message}");
            return false;
        }
    }

    public static async Task<bool> OtaWriteAsync(string s)
        => await OtaWriteAsync(Encoding.UTF8.GetBytes(s));

    // ── Notifications ──
    public static async Task SubscribeToNotifications()
    {
        if (ReadCharacteristic == null) return;
        try
        {
            ReadCharacteristic.ValueUpdated -= OnValueUpdated;
            ReadCharacteristic.ValueUpdated += OnValueUpdated;
            await ReadCharacteristic.StartUpdatesAsync();
        }
        catch (Exception ex) { Debug.WriteLine($"Subscribe: {ex.Message}"); }
    }

    private static string _buf = "";
    private static readonly object _bufLock = new();

    private static void OnValueUpdated(
        object sender,
        Plugin.BLE.Abstractions.EventArgs.CharacteristicUpdatedEventArgs e)
    {
        var raw = e.Characteristic.Value;
        if (raw == null || raw.Length == 0) return;
        var incoming = Encoding.UTF8.GetString(raw);
        Debug.WriteLine($"[BLE RAW IN]: {incoming.TrimEnd('\n', '\r')}");

        if (OtaMode)
        {
            var trimmed = incoming.Trim();
            if (!string.IsNullOrWhiteSpace(trimmed))
                MainThread.BeginInvokeOnMainThread(
                    () => OnOtaRawReceived?.Invoke(trimmed));
            return;
        }

        lock (_bufLock)
        {
            _buf += incoming;
            while (true)
            {
                int nl = _buf.IndexOf('\n');
                if (nl >= 0)
                {
                    var line = _buf.Substring(0, nl).Trim();
                    _buf = _buf.Substring(nl + 1);
                    if (!string.IsNullOrWhiteSpace(line))
                        MainThread.BeginInvokeOnMainThread(
                            () => OnDataReceived?.Invoke(line));
                }
                else
                {
                    var t = _buf.Trim();
                    if (IsStandalone(t))
                    {
                        _buf = "";
                        MainThread.BeginInvokeOnMainThread(
                            () => OnDataReceived?.Invoke(t));
                    }
                    break;
                }
            }
        }
    }

    private static bool IsStandalone(string s) => s switch
    {
        "READY" or "OK" or "OK_RESTART" or "ERROR"
            or "THERM_OK" or "FTIMER_OK" or "RTIMER_OK" or "MODE_OK" => true,
        _ => false
    };

    // forget = true (alapértelmezés): a felhasználó kézzel bontja a kapcsolatot -> az eszközt
    // elfelejtjük, nincs automatikus újracsatlakozás. forget = false (pl. firmware-frissítés /
    // gyári visszaállítás után, amikor az eszköz újraindul): a mentett eszköz marad, és az app
    // magától újra csatlakozik, amint az eszköz újra elérhető.
    public static async Task DisconnectDeviceAsync(bool forget = true)
    {
        if (forget) { _autoReconnectEnabled = false; Preferences.Set("BTAutoOff", true); CancelReconnect(); }
        if (ConnectedDevice == null) { if (forget) SaveConnectedDevice(null); return; }
        try   { await Adapter.DisconnectDeviceAsync(ConnectedDevice); }
        catch { }
        finally
        {
            HandleDisconnect();
            if (forget) SaveConnectedDevice(null);
        }
    }

    // ── Mentett eszközök (több eszköz + hely-név) ──
    private static string _sessionLastId;

    public static List<SavedDevice> GetSavedDevices()
    {
        try
        {
            var json = Preferences.Get("BTDevices", "");
            var list = string.IsNullOrEmpty(json)
                ? new List<SavedDevice>()
                : (JsonSerializer.Deserialize<List<SavedDevice>>(json) ?? new List<SavedDevice>());
            if (list.Count == 0)
            {
                // régi, egyeszközös mentés átvétele
                var oldId = Preferences.Get("BTDeviceId", null);
                if (!string.IsNullOrEmpty(oldId))
                {
                    list.Add(new SavedDevice { Id = oldId, Name = Preferences.Get("BTDeviceName", "") });
                    SaveDeviceList(list);
                }
            }
            return list;
        }
        catch { return new List<SavedDevice>(); }
    }

    private static void SaveDeviceList(List<SavedDevice> list)
        => Preferences.Set("BTDevices", JsonSerializer.Serialize(list));

    public static SavedDevice FindSaved(string id) => GetSavedDevices().FirstOrDefault(d => d.Id == id);

    // Megjelenítendő név: "Hely (BLE-név)", vagy csak a BLE-név, ha nincs hely megadva.
    public static string DisplayNameFor(string id, string fallbackName)
    {
        var s = FindSaved(id);
        string name = !string.IsNullOrWhiteSpace(s?.Name) ? s.Name : fallbackName;
        return string.IsNullOrWhiteSpace(s?.Location) ? name : $"{s.Location} ({name})";
    }

    public static void SetLocation(string id, string name, string location)
    {
        var list = GetSavedDevices();
        var s = list.FirstOrDefault(d => d.Id == id);
        if (s == null) { s = new SavedDevice { Id = id, Name = name ?? "" }; list.Add(s); }
        s.Location = location ?? "";
        SaveDeviceList(list);
    }

    public static void ForgetSaved(string id)
    {
        var list = GetSavedDevices();
        list.RemoveAll(d => d.Id == id);
        SaveDeviceList(list);
        if (_sessionLastId == id) _sessionLastId = null;
        if (Preferences.Get("BTDeviceId", null) == id) { Preferences.Remove("BTDeviceId"); Preferences.Remove("BTDeviceName"); }
    }

    // device == null: a "legutóbb csatlakozott" jelölés törlése (a mentett lista megmarad).
    public static void SaveConnectedDevice(IDevice device)
    {
        if (device == null)
        {
            Preferences.Remove("BTDeviceId"); Preferences.Remove("BTDeviceName");
            _sessionLastId = null;
            return;
        }
        string id = device.Id.ToString();
        var list = GetSavedDevices();
        var s = list.FirstOrDefault(d => d.Id == id);
        if (s == null) { s = new SavedDevice { Id = id }; list.Add(s); }
        if (!string.IsNullOrEmpty(device.Name)) s.Name = device.Name;
        SaveDeviceList(list);
        Preferences.Set("BTDeviceId", id);
        Preferences.Set("BTDeviceName", s.Name ?? "");
        Preferences.Set("BTAutoOff", false);
        _sessionLastId = id;
        _autoReconnectEnabled = true; // kézi csatlakozás után újra él az automatikus újracsatlakozás
    }

    public static string GetSavedDeviceId() => Preferences.Get("BTDeviceId", null);
    public static string GetSavedDeviceName()
    {
        var id = GetSavedDeviceId();
        return id == null ? null : DisplayNameFor(id, Preferences.Get("BTDeviceName", ""));
    }

    // Több mentett eszköz esetén: a hatótávban lévők közül a felhasználó választ.
    private static bool _pickerBusy;
    public static async Task ShowDevicePickerAsync()
    {
        if (_pickerBusy || Adapter == null || IsConnected) return;
        _pickerBusy = true;
        try
        {
            var perm = await MainThread.InvokeOnMainThreadAsync(
                async () => await Permissions.CheckStatusAsync<Permissions.Bluetooth>());
            if (perm != PermissionStatus.Granted || !Plugin.BLE.CrossBluetoothLE.Current.IsOn) return;

            var found = new List<IDevice>();
            void OnFound(object s, Plugin.BLE.Abstractions.EventArgs.DeviceEventArgs e)
            {
                if (!string.IsNullOrEmpty(e.Device.Name) && !found.Any(d => d.Id == e.Device.Id)) found.Add(e.Device);
            }
            Adapter.DeviceDiscovered += OnFound;
            try { Adapter.ScanTimeout = 4000; await Adapter.StartScanningForDevicesAsync(); }
            finally { Adapter.DeviceDiscovered -= OnFound; }

            var saved = GetSavedDevices();
            var cand = found.Where(d => saved.Any(s => s.Id == d.Id.ToString()))
                            .OrderByDescending(d => d.Rssi).ToList();
            if (cand.Count == 0 || IsConnected) return;

            var labels = cand.Select(d => $"{DisplayNameFor(d.Id.ToString(), d.Name)}  ({d.Rssi} dBm)").ToList();
            string choice = await MainThread.InvokeOnMainThreadAsync(() =>
                Application.Current.MainPage.DisplayActionSheet("Melyik eszközre csatlakozzak?", "Mégse", null, labels.ToArray()));
            int idx = labels.IndexOf(choice);
            if (idx < 0) return;

            var dev = cand[idx];
            await Adapter.ConnectToDeviceAsync(dev);
            await SetupConnectionAsync(dev, auto: true);
        }
        catch (Exception ex) { Debug.WriteLine($"Eszközválasztó: {ex.Message}"); }
        finally { _pickerBusy = false; }
    }

    public static async Task<int> RequestMtuAsync(int requested = 517)
    {
        if (ConnectedDevice == null) return 23;
        try
        {
            var mtu = await ConnectedDevice.RequestMtuAsync(requested);
            Debug.WriteLine($"MTU: {mtu}");
            await Task.Delay(400);
            return mtu;
        }
        catch (Exception ex) { Debug.WriteLine($"MTU hiba: {ex.Message}"); return 23; }
    }
}
