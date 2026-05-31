#pragma once
#include <string>
#include <cstdint>

// Polls the GitHub Releases API for a newer version of the application.
// Designed to be called from the service monitor loop — all methods are
// synchronous and run on the caller's thread.
class UpdateChecker {
public:
    struct UpdateInfo {
        bool        available = false;
        std::string version;  // e.g. "v1.2.3"
        std::string url;      // browser_download_url of the portable zip
    };

    // Called every monitor loop tick.  Returns true (and populates the pending
    // update) only when all three conditions hold:
    //   1. update_check_enabled is true
    //   2. intervalHours > 0 and enough time has passed since lastCheckUtc
    //   3. the fetched release tag is strictly newer than WSL2IPFWD_VERSION
    bool Tick(bool enabled, int intervalHours, int64_t lastCheckUtc);

    // Bypass the schedule and run a check immediately.
    // Returns true when a newer version is found and pending info is set.
    bool CheckNow();

    // True if the last call to CheckNow / Tick actually reached GitHub
    // (even when no new version was found).  False on network errors.
    bool LastCheckSucceeded() const { return lastCheckSucceeded_; }

    // Unix timestamp set by the last CheckNow / Tick invocation.
    int64_t LastCheckUtc() const { return lastCheckUtc_; }

    // Most-recently found update.  Cleared when a successful check finds no
    // newer version; retained unchanged on network errors.
    UpdateInfo GetPendingUpdate() const { return pending_; }

    // Compare two version strings of the form [v]MAJOR.MINOR.PATCH.
    // Returns true when 'latest' is strictly greater than 'current'.
    static bool IsNewerVersion(const std::string& latest, const std::string& current);

private:
    bool FetchLatestRelease(std::string& outVersion, std::string& outUrl);
    static bool ParseVersion(const std::string& s, int& major, int& minor, int& patch);

    UpdateInfo pending_;
    int64_t    lastCheckUtc_       = 0;
    bool       lastCheckSucceeded_ = false;
};
