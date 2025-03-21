using System.IO.Ports;

namespace Visualisierung_BlazorApp.Services
{
    public class SerialPortService : IDisposable
    {
        public event Action<string>? OnDataReceived;
        private readonly SerialPort _serialPort;

        public SerialPortService()
        {
            _serialPort = new SerialPort("COM3", 9600, Parity.None, 8, StopBits.One);
            _serialPort.DataReceived += SerialPort_DataReceived;
            try
            {
                _serialPort.Open();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Fehler beim Öffnen des COM-Ports: {ex.Message}");
            }
        }

        private void SerialPort_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                var data = _serialPort.ReadExisting();
                OnDataReceived?.Invoke(data);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Fehler beim Lesen der Daten: {ex.Message}");
            }
        }

        public void WriteData(string data)
        {
            try
            {
                if (_serialPort.IsOpen)
                {
                    _serialPort.Write(data);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Fehler beim Schreiben auf den COM-Port: {ex.Message}");
            }
        }

        public void Dispose()
        {
            if (_serialPort == null) return;
            if (_serialPort.IsOpen)
            {
                _serialPort.Close();
            }
            _serialPort.Dispose();
        }
    }
}