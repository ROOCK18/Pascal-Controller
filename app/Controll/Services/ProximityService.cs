using System.Diagnostics;
using Plugin.BLE.Abstractions.Contracts;

namespace Controll.Services;

// Közeledés érzékelés: a háttérben (az Android előtér-szolgáltatással együtt) keresi a mentett eszközöket,
// és ha egy ilyen a beállított távolságon (méter) belülre kerül, csatlakozik hozzá, és végrehajtja a hozzárendelt
// műveletet (pl. a kapu relé impulzusát). A távolság a BLE jelerősségből (RSSI) becsült érték: falak, test, telefon
// és környezet miatt több méteres eltérés is lehet, ezért csak tájékoztató jellegű.
public static class ProximityService
{
    // Becslés: d = 10 ^ ((P1m - RSSI) / (10 * n)); P1m = a jelerősség 1 méteren, n = terjedési kitevő (szabadtér 2, beltér 2-3).
    private const double RssiAt1m = -62.0;
    private const double PathLoss = 2.4;
    public static double EstimateMeters(double rssi) => Math.Pow(10.0, (RssiAt1m - rssi) / (10.0 * PathLoss));

    private static CancellationTokenSource? _cts;
    private static Task? _task;
    private static readonly object _lock = new();
    private static readonly Dictionary<string, List<int>> _hist = new();   // utolsó RSSI-k (simításhoz)
    private static readonly Dictionary<string, bool> _armed = new();       // true: a következő közeledésnél végrehajthat
    private static readonly Dictionary<string, DateTime> _lastSeen = new();
    private static readonly Dictionary<string, DateTime> _outsideSince = new();   // mióta van folyamatosan a "kívül" távolságon

    public static bool AnyEnabled => BluetoothManager.GetSavedDevices().Any(d => d.ProxEnabled);

    public static string ActionLabel(string key) => key switch
    {
        "r1pulse" => "Relé 1 impulzus",
        "r2pulse" => "Relé 2 impulzus",
        "r1on" => "Relé 1 BE",
        "r1off" => "Relé 1 KI",
        "r2on" => "Relé 2 BE",
        "r2off" => "Relé 2 KI",
        "rgbon" => "RGB BE",
        "rgboff" => "RGB KI",
        _ => "Csak csatlakozás"
    };

    // A beállítások változása után hívandó: elindítja vagy leállítja a figyelést.
    public static void Refresh()
    {
        lock (_lock)
        {
            bool running = _task is { IsCompleted: false };
            if (AnyEnabled && !running)
            {
                _cts = new CancellationTokenSource();
                var ct = _cts.Token;
                _task = Task.Run(() => RunAsync(ct));
            }
            else if (!AnyEnabled && running)
            {
                _cts?.Cancel();
            }
        }
    }

    private static async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            if (!AnyEnabled) break;
            // Több mentett eszköz esetén mindig az UTOLJÁRA használt eszközt figyeli
            string? last = BluetoothManager.LastUsedId;
            var cfgs = BluetoothManager.GetSavedDevices().Where(d => d.ProxEnabled && d.Id == last).ToList();
            if (cfgs.Count == 0) { try { await Task.Delay(10000, ct); } catch { break; } continue; }
            try
            {
                if (!await BleReadyAsync()) { await Task.Delay(15000, ct); continue; }
                if (BluetoothManager.IsConnected) await PollConnectedAsync(cfgs);
                else if (BluetoothManager.AutoConnectAllowed) await ScanAndConnectAsync(cfgs);
                await Task.Delay(BluetoothManager.IsConnected ? 4000 : 6000, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex) { Debug.WriteLine($"Közeledés: {ex.Message}"); try { await Task.Delay(8000, ct); } catch { break; } }
        }
    }

    private static async Task<bool> BleReadyAsync()
    {
        var perm = await MainThread.InvokeOnMainThreadAsync(
            async () => await Permissions.CheckStatusAsync<Permissions.Bluetooth>());
        return perm == PermissionStatus.Granted && Plugin.BLE.CrossBluetoothLE.Current.IsOn;
    }

    private static double SmoothedMeters(string id, int rssi)
    {
        if (!_hist.TryGetValue(id, out var h)) _hist[id] = h = new List<int>();
        h.Add(rssi);
        while (h.Count > 3) h.RemoveAt(0);
        return EstimateMeters(h.Average());
    }

    private static bool IsArmed(string id) => _armed.TryGetValue(id, out var a) && a;

    private static async Task ScanAndConnectAsync(List<SavedDevice> cfgs)
    {
        var ids = cfgs.ToDictionary(c => c.Id);
        var seen = new Dictionary<string, IDevice>();
        var rssi = new Dictionary<string, List<int>>();
        var gate = new object();

        void OnAdv(object? s, Plugin.BLE.Abstractions.EventArgs.DeviceEventArgs e)
        {
            string id = e.Device.Id.ToString();
            if (!ids.ContainsKey(id)) return;
            lock (gate)
            {
                seen[id] = e.Device;
                if (!rssi.TryGetValue(id, out var l)) rssi[id] = l = new List<int>();
                l.Add(e.Device.Rssi);
            }
        }

        var adapter = BluetoothManager.Adapter;
        adapter.DeviceDiscovered += OnAdv;
        adapter.DeviceAdvertised += OnAdv;
        try { adapter.ScanTimeout = 4000; await adapter.StartScanningForDevicesAsync(); }
        finally { adapter.DeviceDiscovered -= OnAdv; adapter.DeviceAdvertised -= OnAdv; }

        var now = DateTime.UtcNow;
        SavedDevice? best = null; double bestDist = double.MaxValue;
        foreach (var cfg in cfgs)
        {
            if (!seen.TryGetValue(cfg.Id, out _) || !rssi.TryGetValue(cfg.Id, out var list) || list.Count == 0)
            {
                UpdateArming(cfg, null, now);   // nem látszik
                continue;
            }
            double dist = SmoothedMeters(cfg.Id, (int)Math.Round(list.Average()));
            UpdateArming(cfg, dist, now);
            _lastSeen[cfg.Id] = now;
            if (dist <= cfg.ProxMeters && dist < bestDist) { best = cfg; bestDist = dist; }
        }

        if (best == null || BluetoothManager.IsConnected) return;
        if (await BluetoothManager.ConnectNearbyAsync(seen[best.Id]))
            await RunActionIfArmedAsync(best);
    }

    private static async Task PollConnectedAsync(List<SavedDevice> cfgs)
    {
        var dev = BluetoothManager.ConnectedDevice;
        if (dev == null) return;
        string id = dev.Id.ToString();
        var cfg = cfgs.FirstOrDefault(c => c.Id == id);
        if (cfg == null) return;
        if (!await dev.UpdateRssiAsync()) return;

        double dist = SmoothedMeters(id, dev.Rssi);
        var now = DateTime.UtcNow;
        UpdateArming(cfg, dist, now);
        _lastSeen[id] = now;
        if (dist <= cfg.ProxMeters) await RunActionIfArmedAsync(cfg);
    }

    // Az újraélesedés szabálya (SavedDevice.ProxRearmMode). dist == null: az eszköz most nem látszik.
    private static void UpdateArming(SavedDevice cfg, double? dist, DateTime now)
    {
        string id = cfg.Id;
        double unseenFor = dist == null ? (now - _lastSeen.GetValueOrDefault(id, DateTime.MinValue)).TotalSeconds : 0;
        switch (cfg.ProxRearmMode)
        {
            case 0:   // teljes kapcsolatszakadás: az eszköz legalább 15 mp-ig egyáltalán nem látszik
                if (dist == null && unseenFor >= 15) _armed[id] = true;
                break;
            case 1:   // ha a megadott méteren kívül van (mérhetően, vagy 15 mp-ig nem látszik)
                if (dist != null ? dist > cfg.ProxRearmMeters : unseenFor >= 15) _armed[id] = true;
                break;
            default:  // a megadott méteren kívül van legalább ProxRearmSec másodpercig (nem látszó eszköz is "kívül")
                if (dist == null || dist > cfg.ProxRearmMeters)
                {
                    if (!_outsideSince.TryGetValue(id, out var since)) _outsideSince[id] = since = now;
                    if ((now - since).TotalSeconds >= cfg.ProxRearmSec) _armed[id] = true;
                }
                else _outsideSince.Remove(id);
                break;
        }
    }

    private static async Task RunActionIfArmedAsync(SavedDevice cfg)
    {
        if (!IsArmed(cfg.Id)) return;
        _armed[cfg.Id] = false;   // egy közeledés = egy végrehajtás
        _outsideSince.Remove(cfg.Id);
        string? cmd = cfg.ProxAction switch
        {
            "r1pulse" => $"PULSE:R1:{cfg.ProxPulseSec}",
            "r2pulse" => $"PULSE:R2:{cfg.ProxPulseSec}",
            "r1on" => "R1_1",
            "r1off" => "R1_0",
            "r2on" => "R2_1",
            "r2off" => "R2_0",
            "rgbon" => "RGB_1",
            "rgboff" => "RGB_0",
            _ => null
        };
        if (cmd != null)
        {
            await BluetoothManager.WriteAsync(cmd);
            await Task.Delay(150);
        }
        await BluetoothManager.WriteAsync("PROX_TRIGGER");   // az eszköz a Vezérlés oldalon beállított közeledés-automatikákat is végrehajtja (ha az automatika be van kapcsolva)
        NotificationService.Show("Közeledés érzékelve",
            $"{BluetoothManager.DisplayNameFor(cfg.Id, cfg.Name)}: {ActionLabel(cfg.ProxAction)} végrehajtva.", 3000);
    }
}
