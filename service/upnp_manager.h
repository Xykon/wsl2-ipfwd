#pragma once
#include <windows.h>
#include <string>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

// One desired router port mapping. Keyed externally by router/external port;
// the internal client IP is determined by the manager (host LAN IP).
struct UpnpMapping {
    uint16_t    internalPort = 0;   // Windows-side local port the router targets
    std::string description;
};

// Manages UPnP IGD port mappings via the miniupnpc library.
//
// miniupnpc does its own SSDP discovery over raw UDP, so it works from a service
// running as LocalSystem in session 0 (unlike the Windows IUPnPNAT COM API, which
// often fails to discover the IGD there).
//
// Runs entirely on its own worker thread: SSDP discovery blocks for a couple of
// seconds, so it must never run on the service monitor thread.  The monitor
// declares the full desired set each cycle via SetDesired(); the worker
// reconciles asynchronously and re-asserts periodically (routers may expire
// mappings).
class UpnpManager {
public:
    UpnpManager() = default;
    ~UpnpManager();

    void Start();
    void Stop();   // removes all mappings it created, then joins the worker

    // Replace the full set of desired external→internal mappings (keyed by
    // external/router port). Reconciled asynchronously.
    void SetDesired(std::map<uint16_t, UpnpMapping> desired);

    void SetLogger(std::function<void(const std::string&)> logger);

    // Thread-safe snapshot of the external (router) ports currently mapped.
    // Safe to call from any thread (e.g. the IPC handler).
    std::set<uint16_t> ActiveExternalPorts() const;

private:
    void WorkerLoop();
    bool EnsureIgd();          // discover & cache the IGD (controlURL, service, LAN IP)
    void ReleaseIgd();
    void Reconcile(bool reassert);
    void RemoveAllActive();
    void PublishActive();      // copy active_ keys into the thread-safe snapshot
    void Log(const std::string& m);

    std::mutex                      mtx_;
    std::map<uint16_t, UpnpMapping> desired_;       // guarded by mtx_

    std::map<uint16_t, UpnpMapping> active_;        // worker-thread only
    std::string                     activeHostIp_;  // worker-thread only

    std::atomic<bool> running_{false};
    std::thread       thread_;
    HANDLE            wake_{nullptr};

    // Cached IGD handle (miniupnpc UPNPUrls + IGDdatas + LAN IP). Opaque here so
    // the miniupnpc headers stay confined to the .cpp.
    struct Igd;
    Igd* igd_ = nullptr;       // worker-thread only

    // Thread-safe published copy of the currently-mapped external ports.
    mutable std::mutex     pubMtx_;
    std::set<uint16_t>     activePublished_;   // guarded by pubMtx_

    std::function<void(const std::string&)> logger_;
};
