namespace Controll.Services;

public interface IBluetoothService
{
    bool IsConnected { get; }
    Task<bool> ConnectAsync();
    Task WriteAsync(string message);
}