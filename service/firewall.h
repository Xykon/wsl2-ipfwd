#pragma once
#include <string>
#include <cstdint>

// Firewall profile flags (match NET_FW_PROFILE_TYPE2_ values)
enum FwProfile : long {
    FW_DOMAIN  = 1,
    FW_PRIVATE = 2,
    FW_PUBLIC  = 4,
    FW_ALL     = 7
};

class FirewallManager {
public:
    FirewallManager();
    ~FirewallManager();

    bool Initialize();
    void Uninitialize();

    // Add an inbound TCP allow rule for the given port
    bool AddRule(uint16_t port, long profiles);

    // Remove the rule for the given port (by our canonical rule name)
    bool RemoveRule(uint16_t port);

    // Check if a rule exists
    bool RuleExists(uint16_t port);

    static std::wstring RuleName(uint16_t port);

private:
    void* policy_  = nullptr; // INetFwPolicy2*
    void* rules_   = nullptr; // INetFwRules*
    bool  initialized_ = false;
};
