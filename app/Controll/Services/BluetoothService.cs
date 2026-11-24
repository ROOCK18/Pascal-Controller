using Plugin.BLE;
using Plugin.BLE.Abstractions;
using Plugin.BLE.Abstractions.Contracts;
using System.Text;

namespace Controll.Services;

public class BluetoothService : IBluetoothService
{
    private readonly IAdapter _adapter;
    private IDevice? _device;
    private ICharacteristic? _txCharacteristic; // MAUI szempontból amin ÍRUNK (RX az ESP-n)

    // Az ESP32 kódodból származó UUID-k
    private readonly Guid ServiceGuid = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    private readonly Guid RxGuid = Guid.Parse("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");

    public bool IsConnected => _device != null && _device.State == DeviceState.Connected;

    public BluetoothService()
    {
        _adapter = CrossBluetoothLE.Current.Adapter;
    }

    public async Task<bool> ConnectAsync()
    {
        if (IsConnected) return true;

        try
        {
            // 1. Keresés indítása
            _adapter.DeviceDiscovered += (s, a) =>
            {
                if (a.Device.Name == "TTGO-Terrarium")
                    _device = a.Device;
            };

            await _adapter.StartScanningForDevicesAsync();

            if (_device == null) return false;

            // 2. Csatlakozás
            await _adapter.ConnectToDeviceAsync(_device);

            // 3. Service és Karakterisztika lekérése
            var service = await _device.GetServiceAsync(ServiceGuid);
            if (service != null)
            {
                _txCharacteristic = await service.GetCharacteristicAsync(RxGuid);
            }

            return IsConnected;
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Hiba: {ex.Message}");
            return false;
        }
    }

    public async Task WriteAsync(string message)
    {
        if (!IsConnected || _txCharacteristic == null) return;

        try
        {
            var bytes = Encoding.UTF8.GetBytes(message);
            await _txCharacteristic.WriteAsync(bytes);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Küldési hiba: {ex.Message}");
        }
    }
}