namespace Controll.Services;

// Az eszköz riasztásait ("ALERT:<szenzor>:<állapot>:<érték>") telefonos értesítéssé alakítja.
// Állapot: 0 = megszűnt, 1 = szenzorhiba, 2 = túl alacsony, 3 = túl magas hőmérséklet.
// Az app akkor is feldolgozza őket, ha nincs megnyitva a Beállítások oldal (az App indulásakor iratkozik fel).
public static class AlertService
{
    private static bool _init;
    // Eszköz+szenzor -> utolsó jelzett állapot (ugyanazt a riasztást ne jelezzük újra meg újra, pl. újracsatlakozáskor).
    private static readonly Dictionary<string, int> _last = new();

    public static void Initialize()
    {
        if (_init) return;
        _init = true;
        BluetoothManager.OnDataReceived += OnLine;
    }

    private static void OnLine(string line)
    {
        if (!line.StartsWith("ALERT:")) return;
        var p = line.Substring(6).Trim().Split(':');
        if (p.Length < 3 || !int.TryParse(p[0], out int sensor) || !int.TryParse(p[1], out int state)) return;
        string val = p[2] == "nan" ? "--" : p[2];

        string devId = BluetoothManager.ConnectedDevice?.Id.ToString() ?? "";
        string key = devId + "/" + sensor;
        _last.TryGetValue(key, out int prev);
        if (prev == state) return;
        _last[key] = state;

        string where = devId.Length > 0 ? BluetoothManager.DisplayNameFor(devId, "Pascal-Controller") : "Pascal-Controller";
        (string title, string text)? msg = state switch
        {
            1 => ($"Szenzorhiba – {where}", $"A(z) {sensor}. hőmérő nem ad értelmezhető értéket (szakadt vagy rövidzárlatos szál?)."),
            2 => ($"Túl alacsony hőmérséklet – {where}", $"{sensor}. hőmérő: {val} °C"),
            3 => ($"Túl magas hőmérséklet – {where}", $"{sensor}. hőmérő: {val} °C"),
            0 when prev != 0 => ($"Riasztás megszűnt – {where}", $"{sensor}. hőmérő: {val} °C"),
            _ => null
        };
        if (msg != null) NotificationService.Show(msg.Value.title, msg.Value.text, 2000 + sensor);
    }
}
