#include "ipc_server.h"
#include "../common/protocol.h"
#include <sddl.h>
#include <vector>

#pragma comment(lib, "advapi32.lib")

IpcServer::IpcServer(RequestHandler handler) : handler_(std::move(handler)) {}

IpcServer::~IpcServer() {
    Stop();
}

bool IpcServer::Start() {
    if (running_) return true;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;
    running_ = true;
    acceptThread_ = std::thread(&IpcServer::AcceptLoop, this);
    return true;
}

void IpcServer::Stop() {
    if (!running_) return;
    running_ = false;
    if (stopEvent_ != INVALID_HANDLE_VALUE) {
        SetEvent(stopEvent_);
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    if (stopEvent_ != INVALID_HANDLE_VALUE) {
        CloseHandle(stopEvent_);
        stopEvent_ = INVALID_HANDLE_VALUE;
    }
}

// Overlapped helper: read exactly `sz` bytes from an overlapped pipe handle.
static bool ReadFull(HANDLE pipe, void* buf, DWORD sz) {
    DWORD total = 0;
    while (total < sz) {
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD n = 0;
        BOOL ok = ReadFile(pipe, static_cast<char*>(buf) + total, sz - total, &n, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                WaitForSingleObject(ov.hEvent, INFINITE);
                ok = GetOverlappedResult(pipe, &ov, &n, FALSE);
            }
        }
        CloseHandle(ov.hEvent);
        if (!ok || n == 0) return false;
        total += n;
    }
    return true;
}

// Overlapped helper: write all bytes from `buf`.
static bool WriteFull(HANDLE pipe, const void* buf, DWORD sz) {
    DWORD total = 0;
    while (total < sz) {
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD n = 0;
        BOOL ok = WriteFile(pipe, static_cast<const char*>(buf) + total, sz - total, &n, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                WaitForSingleObject(ov.hEvent, INFINITE);
                ok = GetOverlappedResult(pipe, &ov, &n, FALSE);
            }
        }
        CloseHandle(ov.hEvent);
        if (!ok || n == 0) return false;
        total += n;
    }
    return true;
}

bool IpcServer::ReadMessage(HANDLE pipe, std::string& out) {
    uint32_t len = 0;
    if (!ReadFull(pipe, &len, sizeof(len))) return false;
    if (len == 0 || len > 1024 * 1024) return false;
    out.resize(len);
    return ReadFull(pipe, out.data(), len);
}

bool IpcServer::WriteMessage(HANDLE pipe, const std::string& msg) {
    uint32_t len = static_cast<uint32_t>(msg.size());
    if (!WriteFull(pipe, &len, sizeof(len))) return false;
    return WriteFull(pipe, msg.data(), len);
}

void IpcServer::ClientThread(HANDLE pipe) {
    ++activeClientCount_;
    while (running_) {
        std::string request;
        if (!ReadMessage(pipe, request)) break;
        std::string response = handler_(request);
        if (!WriteMessage(pipe, response)) break;
    }
    --activeClientCount_;
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

void IpcServer::AcceptLoop() {
    SECURITY_DESCRIPTOR sd = {};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE); // NULL DACL = allow all local

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    while (running_) {
        // FILE_FLAG_OVERLAPPED required for interruptible ConnectNamedPipe
        HANDLE pipe = CreateNamedPipeW(
            WSL2IPFWD_PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024, 64 * 1024, 0, &sa);

        if (pipe == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(stopEvent_, 1000) == WAIT_OBJECT_0) break;
            continue;
        }

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        // connected == FALSE is normal for overlapped — check pending vs already connected
        DWORD err = GetLastError();

        bool proceed = false;
        if (!connected && err == ERROR_IO_PENDING) {
            HANDLE events[2] = { ov.hEvent, stopEvent_ };
            DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0) {
                // Stop requested
                CancelIo(pipe);
                CloseHandle(ov.hEvent);
                CloseHandle(pipe);
                break;
            }
            DWORD transferred = 0;
            proceed = GetOverlappedResult(pipe, &ov, &transferred, FALSE) != FALSE;
        } else if (!connected && err == ERROR_PIPE_CONNECTED) {
            proceed = true; // client connected before we called ConnectNamedPipe
        } else if (connected) {
            proceed = true; // shouldn't happen on overlapped, but handle it
        }

        CloseHandle(ov.hEvent);

        if (!proceed) {
            CloseHandle(pipe);
            continue;
        }

        std::thread([this, pipe]() { ClientThread(pipe); }).detach();
    }
}
