#include "ServiceRegistration.h"
#include "Daemon/ServiceRegistrationPolicy.h"
#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace {
QVariantMap registration(const QString &command, NSString *plist, const QString &label)
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            using Operation = society::daemon::RegistrationOperation;
            auto operation = Operation::Status;
            if (command == "register") operation = Operation::Register;
            else if (command == "unregister") operation = Operation::Unregister;
            else if (command != "status")
                return {{"ok", false}, {"error", "Use register, status or unregister."}};
            SMAppService *service = [SMAppService agentServiceWithPlistName:plist];
            NSError *error = nil;
            const auto outcome = society::daemon::registrationOutcome(operation,
                SMAppServiceStatusEnabled, SMAppServiceStatusNotRegistered,
                [service] { return service.status; },
                [service, &error](Operation action) {
                    return action == Operation::Register ? [service registerAndReturnError:&error]
                                                        : [service unregisterAndReturnError:&error];
                });
            QString status;
            switch (outcome.state) {
            case SMAppServiceStatusEnabled: status = "enabled"; break;
            case SMAppServiceStatusRequiresApproval: status = "requires-approval"; break;
            case SMAppServiceStatusNotFound: status = "not-found"; break;
            default: status = "not-registered"; break;
            }
            QVariantMap result{{"ok", outcome.ok}, {"status", status}, {"label", label}};
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
