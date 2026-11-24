using System.Diagnostics;
using Plugin.BLE.Abstractions.Contracts;

namespace Controll.Services;

// Közeledés érzékelés: a háttérben (az Android előtér-szolgáltatással együtt) keresi az UTOLJÁRA HASZNÁLT eszközt,
// és ha a beállított távolságon (méter) belülre kerül, csatlakozik hozzá. A csatornánként (ventilátor 1-2, relé 1-2, RGB)
// megadott távolságon belül az adott csatorna közeledés-műveletét indítja az eszközön (az eszköz csak az Automata állapotú
// csatornát kapcsolja). Minden csatornának saját távolsága és újraélesedési szabálya van.
// A távolság a BLE jelerősségből (RSSI) becsült érték: falak, test, telefon és környezet miatt több méteres eltérés is lehet.
public static class ProximityService
{
    // Becslés: d = 10 ^ ((P1m - RSSI) / (10 * n)); P1m = a jelerősség 1 méteren, n = terjedési kitevő (szabadtér 2, beltér 2-3).
    private const double RssiAt1m = -62.0;
    private const double PathLoss = 2.4;
    public static double EstimateMeters(double rssi) => Math.Pow(10.0, (RssiAt1m - rssi) / (10.0 * PathLoss));

    public static readonly string[] Channels = { "F1", "F2", "R1", "R2", "RGB" };

    private static CancellationTokenSource? _cts;
    private static Task? _task;
    private static readonly object _lock = new();
    private static readonly Dictionary<string, List<int>> _hist = new();      // eszközönként az utolsó RSSI-k (simításhoz)
    private static readonly Dictionary<string, bool> _armed = new();          // "eszköz/csatorna": a következő közeledésnél végrehajthat
    private static readonly Dictionary<string, DateTime> _lastSeen = new();   // eszközönként: mikor látszott utoljára
    private static readonly Dictionary<string, DateTime> _outsideSince = new(); // "eszköz/csatorna": mióta van folyamatosan a "kívül" távolságon

    // A bekapcsolt csatornák egy eszköznél
    private static List<(string key, ProxChannelCfg cfg)> EnabledChannels(SavedDevice d)
        => d.ProxChannels.Where(kv => kv.Value.Enabled && Channels.Contains(kv.Key)).Select(kv => (kv.Key, kv.Value)).ToList();

    // Eddig a távolságig csatlakozik az app az eszközhöz: az automatikus csatlakozás távolsága (Beállítások), illetve a
    // bekapcsolt csatornák legnagyobb távolsága - a csatorna-művelethez kapcsolat kell.
    public static double ConnectMeters(SavedDevice d)
    {
        if (d.ProxEnabled) return double.MaxValue;   // automatikus csatlakozás: azonnal, amint az eszköz látszik (nem távolságfüggő)
        double m = 0;
        foreach (var (_, c) in EnabledChannels(d)) m = Math.Max(m, c.Meters);
        return m;
    }

    public static bool Wanted(SavedDevice d) => ConnectMeters(d) > 0;
    public static bool AnyEnabled => BluetoothManager.GetSavedDevices().Any(Wanted);

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
            try
            {
                // Több mentett eszköz esetén mindig az UTOLJÁRA használt eszközt figyeli
                string? last = BluetoothManager.LastUsedId;
                var cfg = BluetoothManager.GetSavedDevices().FirstOrDefault(d => d.Id == last && Wanted(d));
                if (cfg == null) { await Task.Delay(10000, ct); continue; }

                if (!await BleReadyAsync()) { await Task.Delay(15000, ct); continue; }
                if (BluetoothManager.IsConnected)
                {
                    if (BluetoothManager.ConnectedDevice?.Id.ToString() == cfg.Id) await PollConnectedAsync(cfg);
                }
                else if (BluetoothManager.AutoConnectAllowed) await ScanAndConnectAsync(cfg);
                await Task.Delay(BluetoothManager.IsConnected ? 2000 : 6000, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex) { Debug.WriteLine($"Közeledés: {ex.Message}"); try { await Task.Delay(8000, ct); } catch { break; } }
        }
    }

    private static async Task<bool> BleReadyAsync()
    {
        var perm = await MainThread.InvokeOnMainThreadAsync(
            async () => await Permissions.CheckStatusAsync<Permissions.Bluetooth>());
        return perm == PermissionStatus.Granted;   // kikapcsolt Bluetooth mellett is megy a BLE (Android "Bluetooth-keresés")
    }

    private static double SmoothedMeters(string id, int rssi)
    {
        if (!_hist.TryGetValue(id, out var h)) _hist[id] = h = new List<int>();
        h.Add(rssi);
        while (h.Count > 3) h.RemoveAt(0);
        return EstimateMeters(h.Average());
    }

    private static async Task ScanAndConnectAsync(SavedDevice cfg)
    {
        IDevice? found = null;
        var rssi = new List<int>();
        var gate = new object();

        void OnAdv(object? s, Plugin.BLE.Abstractions.EventArgs.DeviceEventArgs e)
        {
            if (e.Device.Id.ToString() != cfg.Id) return;
            lock (gate) { found = e.Device; rssi.Add(e.Device.Rssi); }
        }

        var adapter = BluetoothManager.Adapter;
        adapter.DeviceDiscovered += OnAdv;
        adapter.DeviceAdvertised += OnAdv;
        try { adapter.ScanTimeout = 4000; await adapter.StartScanningForDevicesAsync(); }
        finally { adapter.DeviceDiscovered -= OnAdv; adapter.DeviceAdvertised -= OnAdv; }

        var now = DateTime.UtcNow;
        IDevice? dev; List<int> list;
        lock (gate) { dev = found; list = rssi.ToList(); }
        if (dev == null || list.Count == 0)
        {
            UpdateAllArming(cfg, null, now);   // nem látszik
            return;
        }

        double dist = SmoothedMeters(cfg.Id, (int)Math.Round(list.Average()));
        UpdateAllArming(cfg, dist, now);
        _lastSeen[cfg.Id] = now;
        if (dist <= ConnectMeters(cfg) && !BluetoothManager.IsConnected && await BluetoothManager.ConnectNearbyAsync(dev))
            await FireEligibleAsync(cfg, dist);
    }

    private static async Task PollConnectedAsync(SavedDevice cfg)
    {
        var dev = BluetoothManager.ConnectedDevice;
        if (dev == null || !await dev.UpdateRssiAsync()) return;

        double dist = SmoothedMeters(cfg.Id, dev.Rssi);
        var now = DateTime.UtcNow;
        UpdateAllArming(cfg, dist, now);
        _lastSeen[cfg.Id] = now;
        await FireEligibleAsync(cfg, dist);
        await CheckRgbLeaveAsync(cfg, dist);
    }

    private static string Key(string id, string ch) => id + "/" + ch;
    private static bool IsArmed(string id, string ch) => _armed.TryGetValue(Key(id, ch), out var a) && a;

    private static void UpdateAllArming(SavedDevice cfg, double? dist, DateTime now)
    {
        foreach (var (ch, c) in EnabledChannels(cfg)) UpdateArming(cfg.Id, ch, c, dist, now);
    }

    // Az újraélesedés szabálya csatornánként (ProxChannelCfg.RearmMode). dist == null: az eszköz most nem látszik.
    private static void UpdateArming(string id, string ch, ProxChannelCfg c, double? dist, DateTime now)
    {
        string key = Key(id, ch);
        double unseenFor = dist == null ? (now - _lastSeen.GetValueOrDefault(id, DateTime.MinValue)).TotalSeconds : 0;
        switch (c.RearmMode)
        {
            case 0:   // teljes kapcsolatszakadás: az eszköz legalább 15 mp-ig egyáltalán nem látszik
                if (dist == null && unseenFor >= 15) _armed[key] = true;
                break;
            case 1:   // ha a megadott méteren kívül van (mérhetően, vagy 15 mp-ig nem látszik)
                if (dist != null ? dist > c.RearmMeters : unseenFor >= 15) _armed[key] = true;
                break;
            default:  // a megadott méteren kívül van legalább RearmSec másodpercig (nem látszó eszköz is "kívül")
                if (dist == null || dist > c.RearmMeters)
                {
                    if (!_outsideSince.TryGetValue(key, out var since)) _outsideSince[key] = since = now;
                    if ((now - since).TotalSeconds >= c.RearmSec) _armed[key] = true;
                }
                else _outsideSince.Remove(key);
                break;
        }
    }

    // A csatornák, amelyek távolságán belül vagyunk, és élesek: a művelet indítása az eszközön (csatornánként)
    private static async Task FireEligibleAsync(SavedDevice cfg, double dist)
    {
        // Értesítést a közeledés nem küld: a telefon csak riasztáskor (szenzorhiba, túl alacsony/magas hőmérséklet) értesít.
        foreach (var (ch, c) in EnabledChannels(cfg))
        {
            if (dist > c.Meters || !IsArmed(cfg.Id, ch)) continue;
            _armed[Key(cfg.Id, ch)] = false;   // egy közeledés = egy végrehajtás
            _outsideSince.Remove(Key(cfg.Id, ch));
            await BluetoothManager.WriteAsync($"PROX_TRIGGER:{ch}");
            await Task.Delay(150);
            if (ch == "RGB") _rgbInside.Add(cfg.Id);
        }
    }

    // RGB "amíg ott vagyok" mód: ha a telefon az újraélesedés távolságán (X méter) kívülre megy, jelezzük az eszköznek a távozást.
    // (Ha a kapcsolat megszakad, az eszköz 15 mp után magától távozottnak veszi.)
    private static readonly HashSet<string> _rgbInside = new();

    private static async Task CheckRgbLeaveAsync(SavedDevice cfg, double dist)
    {
        if (!_rgbInside.Contains(cfg.Id)) return;
        if (!cfg.ProxChannels.TryGetValue("RGB", out var c) || !c.Enabled || c.RgbMode != 1) { _rgbInside.Remove(cfg.Id); return; }
        if (dist <= c.RearmMeters) return;
        _rgbInside.Remove(cfg.Id);
        await BluetoothManager.WriteAsync("PROX_LEAVE:RGB");
    }
}
