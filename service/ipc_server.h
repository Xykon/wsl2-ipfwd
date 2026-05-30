#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <windows.h>

// Named pipe IPC server.
// Accepts connections, reads length-prefixed JSON requests,
// calls requestHandler_, writes length-prefixed JSON responses.
class IpcServer {
public:
    using RequestHandler = std::function<std::string(const std::string&)>;

    explicit IpcServer(RequestHandler handler);
    ~IpcServer();

    bool Start();
    void Stop();

    bool IsRunning()        const { return running_; }
    int  ActiveClientCount() const { return activeClientCount_.load(); }

private:
    void AcceptLoop();
    void ClientThread(HANDLE pipe);

    static bool ReadMessage(HANDLE pipe, std::string& out);
    static bool WriteMessage(HANDLE pipe, const std::string& msg);

    RequestHandler    handler_;
    std::thread       acceptThread_;
    std::atomic<bool> running_{false};
    std::atomic<int>  activeClientCount_{0};
    HANDLE            stopEvent_{INVALID_HANDLE_VALUE};
};
