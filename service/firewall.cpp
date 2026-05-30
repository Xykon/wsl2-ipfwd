#include "firewall.h"
#include <windows.h>
#include <netfw.h>
#include <oleauto.h>  // SysAllocString / SysFreeString

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// RAII wrapper for BSTR (avoids ATL/_bstr_t dependency)
struct Bstr {
    BSTR b;
    explicit Bstr(const wchar_t* s) : b(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(b); }
    operator BSTR() const { return b; }
};

std::wstring FirewallManager::RuleName(uint16_t port) {
    return L"WSL2-IPFwd-TCP-" + std::to_wstring(port);
}

FirewallManager::FirewallManager() {}

FirewallManager::~FirewallManager() {
    Uninitialize();
}

bool FirewallManager::Initialize() {
    if (initialized_) return true;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    INetFwPolicy2* policy = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&policy));
    if (FAILED(hr)) return false;

    INetFwRules* rules = nullptr;
    hr = policy->get_Rules(&rules);
    if (FAILED(hr)) { policy->Release(); return false; }

    policy_ = policy;
    rules_  = rules;
    initialized_ = true;
    return true;
}

void FirewallManager::Uninitialize() {
    if (!initialized_) return;
    if (rules_)  { reinterpret_cast<INetFwRules*>(rules_)->Release();   rules_  = nullptr; }
    if (policy_) { reinterpret_cast<INetFwPolicy2*>(policy_)->Release(); policy_ = nullptr; }
    initialized_ = false;
}

bool FirewallManager::AddRule(uint16_t port, long profiles) {
    if (!initialized_ && !Initialize()) return false;

    auto* rules = reinterpret_cast<INetFwRules*>(rules_);

    // Remove existing rule first (idempotent)
    RemoveRule(port);

    INetFwRule* rule = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(INetFwRule), reinterpret_cast<void**>(&rule));
    if (FAILED(hr)) return false;

    rule->put_Name(Bstr(RuleName(port).c_str()));
    rule->put_Description(Bstr(L"WSL2 IP Forwarder - auto-generated inbound rule"));
    rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
    rule->put_LocalPorts(Bstr(std::to_wstring(port).c_str()));
    rule->put_Direction(NET_FW_RULE_DIR_IN);
    rule->put_Enabled(VARIANT_TRUE);
    rule->put_Action(NET_FW_ACTION_ALLOW);
    rule->put_Profiles(profiles);

    hr = rules->Add(rule);
    rule->Release();
    return SUCCEEDED(hr);
}

bool FirewallManager::RemoveRule(uint16_t port) {
    if (!initialized_ && !Initialize()) return false;

    auto* rules = reinterpret_cast<INetFwRules*>(rules_);
    HRESULT hr = rules->Remove(Bstr(RuleName(port).c_str()));
    // HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) or E_INVALIDARG = rule didn't exist
    return SUCCEEDED(hr) || hr == E_INVALIDARG || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

bool FirewallManager::RuleExists(uint16_t port) {
    if (!initialized_ && !Initialize()) return false;

    auto* rules = reinterpret_cast<INetFwRules*>(rules_);
    INetFwRule* rule = nullptr;
    HRESULT hr = rules->Item(Bstr(RuleName(port).c_str()), &rule);
    if (SUCCEEDED(hr) && rule) { rule->Release(); return true; }
    return false;
}
