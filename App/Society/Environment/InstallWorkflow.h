#pragma once

#include <QString>
#include <functional>

/// The host supplies all effects. Empty error strings indicate success; no
/// callback is retained after runFirstInstall returns. The host serializes the
/// callbacks and owns package storage, platform verification and installer UI.
struct FirstInstallPorts final {
    std::function<bool()> targetPresent;
    std::function<QString()> downloadAndCheck;
    std::function<QString()> verifyIdentity;
    std::function<QString()> handoff;
};
/// Handoff means only that the operating-system installer was started.
struct FirstInstallResult final { bool handedOff = false; QString error; };

/// Order the first-install gates and stop immediately on failure or if a target
/// appears. Updating an existing app requires a separate lifecycle coordinator.
[[nodiscard]] FirstInstallResult runFirstInstall(const FirstInstallPorts &ports);
