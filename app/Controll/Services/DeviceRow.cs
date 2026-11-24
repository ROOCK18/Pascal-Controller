using Plugin.BLE.Abstractions.Contracts;

namespace Controll.Services;

// Egy sor az eszközlistában: mentett eszköz (hely-névvel) és/vagy most látott eszköz.
public class DeviceRow
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Location { get; set; } = "";
    public IDevice? Device { get; set; }   // null: most nincs hatótávban

    public string Title => string.IsNullOrWhiteSpace(Location) ? (string.IsNullOrWhiteSpace(Name) ? "Ismeretlen eszköz" : Name) : Location;
    public string Subtitle
    {
        get
        {
            string range = Device != null ? $"hatótávban ({Device.Rssi} dBm)" : "nincs hatótávban";
            return string.IsNullOrWhiteSpace(Location) ? $"{Name} · {range} · ✎ a hely megadásához" : $"{Name} · {range}";
        }
    }
}
