#include <winsock2.h>
#include <ws2def.h>
#include <iphlpapi.h>
#include "net_util.h"
#include <vector>
#include <cstdio>

#pragma comment(lib, "iphlpapi.lib")

bool IsLocalIpv4(const std::string& ip) {
    if (ip.empty()) return false;

    ULONG size = 15000;
    std::vector<unsigned char> buffer(size);
    auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                        GAA_FLAG_SKIP_DNS_SERVER;
    ULONG ret = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        ret = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
    }
    if (ret != NO_ERROR) return false;

    for (auto* cur = addrs; cur; cur = cur->Next) {
        for (auto* ua = cur->FirstUnicastAddress; ua; ua = ua->Next) {
            auto* sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
            if (!sa || sa->sin_family != AF_INET) continue;
            // Format the IPv4 manually (avoids needing WSAStartup / inet_ntop).
            const auto* b = reinterpret_cast<const unsigned char*>(&sa->sin_addr);
            char str[16];
            std::snprintf(str, sizeof(str), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            if (ip == str) return true;
        }
    }
    return false;
}
