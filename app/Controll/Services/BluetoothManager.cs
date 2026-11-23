using Microsoft.Maui.Storage;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions;
using System.Diagnostics;
using System.Text;

namespace Controll;

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
        SaveConnectedDevice(null);
        MainThread.BeginInvokeOnMainThread(() => OnConnectionChanged?.Invoke(false));
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

    public static async Task DisconnectDeviceAsync()
    {
        if (ConnectedDevice == null) return;
        try   { await Adapter.DisconnectDeviceAsync(ConnectedDevice); }
        catch { }
        finally { HandleDisconnect(); }
    }

    public static void SaveConnectedDevice(IDevice device)
    {
        if (device == null)
        { Preferences.Remove("BTDeviceId"); Preferences.Remove("BTDeviceName"); }
        else
        { Preferences.Set("BTDeviceId", device.Id.ToString()); Preferences.Set("BTDeviceName", device.Name); }
    }

    public static string GetSavedDeviceId()   => Preferences.Get("BTDeviceId",   null);
    public static string GetSavedDeviceName() => Preferences.Get("BTDeviceName", null);

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
