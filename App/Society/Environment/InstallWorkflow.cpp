#include "InstallWorkflow.h"

#include <QObject>
#include <utility>

FirstInstallResult runFirstInstall(const FirstInstallPorts &ports) {
    if (!ports.targetPresent || !ports.downloadAndCheck || !ports.verifyIdentity || !ports.handoff)
        return {false, QObject::tr("The installer is not configured.")};
    if (ports.targetPresent()) return {false, QObject::tr("The app is already present. Refresh the catalogue before continuing.")};
    if (auto error = ports.downloadAndCheck(); !error.isEmpty()) return {false, std::move(error)};
    if (auto error = ports.verifyIdentity(); !error.isEmpty()) return {false, std::move(error)};
    if (ports.targetPresent()) return {false, QObject::tr("The app appeared while downloading; installation was stopped.")};
    if (auto error = ports.handoff(); !error.isEmpty()) return {false, std::move(error)};
    return {true, {}};
}
