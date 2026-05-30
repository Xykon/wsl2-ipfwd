// Async named-pipe client for the wsl2ipfwd service.
//
// Usage pattern:
//   var client = new IpcClient();
//   if (await client.ConnectAsync()) {
//       var resp = await client.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdGetStatus });
//       bool ok = resp?["ok"]?.GetValue<bool>() == true;
//   }

using System.IO.Pipes;
using System.Text;
using System.Text.Json.Nodes;

namespace Wsl2IpFwdGui;

/// <summary>
/// Thread-safe, async IPC client for the wsl2ipfwd service named pipe.
/// Wire format: 4-byte LE uint32 body length followed by UTF-8 JSON.
/// </summary>
public sealed class IpcClient : IDisposable
{
    private NamedPipeClientStream? _pipe;
    private int _nextId;

    // Only one request/response in flight at a time — named pipes are full-duplex
    // but the service processes one message per connection sequentially.
    private readonly SemaphoreSlim _lock = new(1, 1);

    public bool IsConnected => _pipe?.IsConnected == true;

    // ---- Connection ---------------------------------------------------------

    /// <summary>
    /// Connect to the service pipe.  Returns true on success.
    /// Safe to call again after a disconnect to reconnect.
    /// </summary>
    public async Task<bool> ConnectAsync(int timeoutMs = 2000)
    {
        try
        {
            _pipe?.Dispose();

            // "." = local machine, PipeOptions.Asynchronous enables ReadAsync/WriteAsync
            _pipe = new NamedPipeClientStream(
                ".", Protocol.PipeName,
                PipeDirection.InOut,
                PipeOptions.Asynchronous);

            await _pipe.ConnectAsync(timeoutMs);
            return true;
        }
        catch
        {
            _pipe?.Dispose();
            _pipe = null;
            return false;
        }
    }

    // ---- Send / receive -----------------------------------------------------

    /// <summary>
    /// Send a JSON request and return the JSON response object.
    /// Automatically stamps the request with a unique "id" field.
    /// Returns null on pipe error or disconnect.
    /// </summary>
    public async Task<JsonObject?> SendAsync(JsonObject request)
    {
        // Stamp each request with a unique ID so in-flight responses can be matched.
        // Interlocked.Increment is thread-safe without needing the lock.
        request["id"] = Interlocked.Increment(ref _nextId);

        await _lock.WaitAsync();
        try
        {
            if (_pipe is null || !_pipe.IsConnected)
                return null;

            // ---- Write request -----------------------------------------------
            string json   = request.ToJsonString();
            byte[] body   = Encoding.UTF8.GetBytes(json);

            // The C++ service reads a 4-byte LE uint32 length prefix
            byte[] lenBuf = BitConverter.GetBytes((uint)body.Length);
            // BitConverter.GetBytes produces native-endian bytes.
            // On x86/x64 Windows IsLittleEndian is always true, but be explicit:
            if (!BitConverter.IsLittleEndian) Array.Reverse(lenBuf);

            await _pipe.WriteAsync(lenBuf);
            await _pipe.WriteAsync(body);
            await _pipe.FlushAsync();

            // ---- Read response -----------------------------------------------
            byte[] respLenBuf = new byte[4];
            await ReadExactAsync(respLenBuf);
            if (!BitConverter.IsLittleEndian) Array.Reverse(respLenBuf);

            uint respLen = BitConverter.ToUInt32(respLenBuf, 0);
            if (respLen == 0 || respLen > 4 * 1024 * 1024)
                throw new InvalidDataException($"Unexpected response length: {respLen}");

            byte[] respBody = new byte[respLen];
            await ReadExactAsync(respBody);

            return JsonNode.Parse(Encoding.UTF8.GetString(respBody)) as JsonObject;
        }
        catch
        {
            // Any pipe error means we've lost the connection
            _pipe?.Dispose();
            _pipe = null;
            return null;
        }
        finally
        {
            _lock.Release();
        }
    }

    // ---- Helpers ------------------------------------------------------------

    /// <summary>Read exactly buf.Length bytes from the pipe (handles partial reads).</summary>
    private async Task ReadExactAsync(byte[] buf)
    {
        int offset = 0;
        while (offset < buf.Length)
        {
            int n = await _pipe!.ReadAsync(buf.AsMemory(offset));
            if (n == 0)
                throw new EndOfStreamException("Pipe closed unexpectedly.");
            offset += n;
        }
    }

    public void Dispose()
    {
        _pipe?.Dispose();
        _lock.Dispose();
    }
}
