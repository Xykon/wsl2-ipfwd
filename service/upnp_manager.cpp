// WIN32_LEAN_AND_MEAN and MINIUPNP_STATICLIB are supplied by the build
// (root CMake defines the former; linking libminiupnpc-static defines the latter).
// winsock2.h must precede windows.h (pulled in by upnp_manager.h).
#include <winsock2.h>
#include <ws2tcpip.h>
#include "upnp_manager.h"
// miniupnpc headers are flat in the library's include/ dir (provided on the
// include path by the linked target), so no "miniupnpc/" prefix.
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// Cached IGD handle. Defined here so miniupnpc types stay out of the header.
struct UpnpManager::Igd {
    struct UPNPUrls  urls{};
    struct IGDdatas  data{};
    char             lanaddr[64] = {0};
    bool             valid = false;
};

UpnpManager::~UpnpManager() { Stop(); }

void UpnpManager::PublishActive() {
    std::set<uint16_t> snap;
    for (auto& [ext, m] : active_) snap.insert(ext);
    std::lock_guard<std::mutex> lk(pubMtx_);
    activePublished_ = std::move(snap);
}

std::set<uint16_t> UpnpManager::ActiveExternalPorts() const {
    std::lock_guard<std::mutex> lk(pubMtx_);
    return activePublished_;
}

void UpnpManager::SetLogger(std::function<void(const std::string&)> logger) {
    logger_ = std::move(logger);
}
void UpnpManager::Log(const std::string& m) { if (logger_) logger_(m); }

void UpnpManager::Start() {
    if (running_) return;
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
    running_ = true;
    thread_ = std::thread(&UpnpManager::WorkerLoop, this);
}

void UpnpManager::Stop() {
    if (!running_) return;
    running_ = false;
    if (wake_) SetEvent(wake_);
    if (thread_.joinable()) thread_.join();
    if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
}

void UpnpManager::SetDesired(std::map<uint16_t, UpnpMapping> desired) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        desired_ = std::move(desired);
    }
    if (wake_) SetEvent(wake_);
}

void UpnpManager::WorkerLoop() {
    WSADATA wsa;
    bool wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    const DWORD reassertMs = 120000; // re-assert mappings every 2 minutes
    while (running_) {
        DWORD r = WaitForSingleObject(wake_, reassertMs);
        if (!running_) break;
        Reconcile(/*reassert=*/ r == WAIT_TIMEOUT);
    }

    RemoveAllActive();
    ReleaseIgd();
    if (wsaOk) WSACleanup();
}

bool UpnpManager::EnsureIgd() {
    if (igd_ && igd_->valid) return true;
    if (!igd_) igd_ = new Igd();

    int error = 0;
    UPNPDev* devlist = upnpDiscover(2000, nullptr, nullptr,
                                    UPNP_LOCAL_PORT_ANY, /*ipv6=*/0, /*ttl=*/2, &error);
    if (!devlist) {
        Log("No UPnP IGD discovered (upnpDiscover error " + std::to_string(error) + ") — will retry.");
        return false;
    }

    int r = UPNP_GetValidIGD(devlist, &igd_->urls, &igd_->data,
                             igd_->lanaddr, sizeof(igd_->lanaddr));
    freeUPNPDevlist(devlist);

    if (r == 1) {   // a connected IGD
        igd_->valid = true;
        Log(std::string("Found IGD; local LAN IP ") + igd_->lanaddr);
        return true;
    }
    if (r > 0) FreeUPNPUrls(&igd_->urls);   // a device was found but not a usable IGD
    Log("No connected IGD found (UPNP_GetValidIGD=" + std::to_string(r) + ") — will retry.");
    return false;
}

void UpnpManager::ReleaseIgd() {
    if (igd_) {
        if (igd_->valid) FreeUPNPUrls(&igd_->urls);
        delete igd_;
        igd_ = nullptr;
    }
}

void UpnpManager::Reconcile(bool reassert) {
    std::map<uint16_t, UpnpMapping> desired;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        desired = desired_;
    }

    // Nothing to do and nothing to clean up → don't even run SSDP discovery.
    if (desired.empty() && active_.empty()) return;
    if (!running_) return;

    if (!EnsureIgd()) return;

    const char* ctrl = igd_->urls.controlURL;
    const char* svc  = igd_->data.first.servicetype;
    std::string hostIp = igd_->lanaddr;
    bool hostChanged = (hostIp != activeHostIp_);
    activeHostIp_ = hostIp;

    // Remove mappings no longer desired (or stale due to a host-IP change).
    for (auto it = active_.begin(); it != active_.end(); ) {
        bool stillWanted = desired.count(it->first) &&
                           desired[it->first].internalPort == it->second.internalPort;
        if (!stillWanted || hostChanged) {
            char ext[8];
            std::snprintf(ext, sizeof(ext), "%u", it->first);
            UPNP_DeletePortMapping(ctrl, svc, ext, "TCP", nullptr);
            Log("Removed UPnP mapping ext " + std::to_string(it->first));
            it = active_.erase(it);
        } else {
            ++it;
        }
    }

    // Add new / changed mappings, and re-assert existing ones on the timer.
    bool anyFailed = false;
    for (auto& [extPort, m] : desired) {
        bool already = active_.count(extPort) &&
                       active_[extPort].internalPort == m.internalPort && !hostChanged;
        if (already && !reassert) continue;

        char ext[8], in[8];
        std::snprintf(ext, sizeof(ext), "%u", extPort);
        std::snprintf(in,  sizeof(in),  "%u", m.internalPort);

        int r = UPNP_AddPortMapping(ctrl, svc, ext, in, hostIp.c_str(),
                                    m.description.c_str(), "TCP",
                                    /*remoteHost=*/nullptr, /*lease=*/"0");
        if (r == UPNPCOMMAND_SUCCESS) {
            if (!already)
                Log("Added UPnP mapping ext " + std::to_string(extPort) +
                    " -> " + hostIp + ":" + std::to_string(m.internalPort));
            active_[extPort] = m;
        } else {
            const char* msg = strupnperror(r);
            Log("AddPortMapping ext " + std::to_string(extPort) + " failed: " +
                std::to_string(r) + " (" + (msg ? msg : "?") + ")");
            anyFailed = true;
        }
    }

    // A failure may mean the cached IGD URL went stale (router rebooted) — drop
    // it so the next cycle rediscovers. Mappings not in active_ are retried then.
    if (anyFailed) ReleaseIgd();

    PublishActive();
}

void UpnpManager::RemoveAllActive() {
    if (!igd_ || !igd_->valid) { active_.clear(); PublishActive(); return; }
    const char* ctrl = igd_->urls.controlURL;
    const char* svc  = igd_->data.first.servicetype;
    for (auto& [extPort, m] : active_) {
        char ext[8];
        std::snprintf(ext, sizeof(ext), "%u", extPort);
        UPNP_DeletePortMapping(ctrl, svc, ext, "TCP", nullptr);
        Log("Removed UPnP mapping ext " + std::to_string(extPort) + " (shutdown)");
    }
    active_.clear();
    PublishActive();
}
