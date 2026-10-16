#include "ServiceRegistration.h"
#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace {
QVariantMap registration(const QString &command, NSString *plist, const QString &label)
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService *service = [SMAppService agentServiceWithPlistName:plist];
            NSError *error = nil;
            bool ok = true;
            if (command == "register") {
                if (service.status != SMAppServiceStatusEnabled)
                    ok = [service registerAndReturnError:&error];
            } else if (command == "unregister") {
                if (service.status != SMAppServiceStatusNotRegistered)
                    ok = [service unregisterAndReturnError:&error];
            } else if (command != "status") {
                return {{"ok", false}, {"error", "Use register, status or unregister."}};
            }
            QString status;
            switch (service.status) {
            case SMAppServiceStatusEnabled: status = "enabled"; break;
            case SMAppServiceStatusRequiresApproval: status = "requires-approval"; break;
            case SMAppServiceStatusNotFound: status = "not-found"; break;
            default: status = "not-registered"; break;
            }
            if (command == "register" && service.status != SMAppServiceStatusEnabled) ok = false;
            QVariantMap result{{"ok", ok}, {"status", status}, {"label", label}};
            if (error) {
                result.insert("error", QString::fromNSString(error.localizedDescription));
                result.insert("errorCode", qint64(error.code));
            }
            return result;
        }
        return {{"ok", false}, {"status", "unsupported"}, {"error", "Society background service registration requires macOS 13 or later."}};
    }
}
}
QVariantMap societyDaemonRegistration(const QString &command)
{
    return registration(command, @"com.iisacc.society.daemon.plist", "com.iisacc.society.daemon");
}
QVariantMap societyContainerMonitorRegistration(const QString &command)
{
    return registration(command, @"com.iisacc.society.container-monitor.plist", "com.iisacc.society.container-monitor");
}
