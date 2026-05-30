#pragma once
#include <string>

// True if the given IPv4 dotted-decimal string is assigned to a local network
// adapter on this host. Used to detect WSL2 "mirrored" networking mode, where
// the WSL2 IP equals one of the Windows host's own interface addresses.
bool IsLocalIpv4(const std::string& ip);
