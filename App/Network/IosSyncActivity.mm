#include "MobileSyncActivity.h"
#include <QDebug>
#import <UIKit/UIKit.h>

namespace { UIBackgroundTaskIdentifier syncTask = UIBackgroundTaskInvalid; }
bool societyBeginBackgroundSync(std::function<void()> expired) {
    __block bool granted = false;
    const auto begin = ^{
        if (syncTask == UIBackgroundTaskInvalid)
            syncTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Society container synchronization"
                expirationHandler:^{
                    const auto task = syncTask; syncTask = UIBackgroundTaskInvalid;
                    if (task != UIBackgroundTaskInvalid) [UIApplication.sharedApplication endBackgroundTask:task];
                    expired();
                }];
        granted = syncTask != UIBackgroundTaskInvalid;
    };
    if (NSThread.isMainThread) begin(); else dispatch_sync(dispatch_get_main_queue(), begin);
    return granted;
}
void societyEndBackgroundSync() {
    const auto end = ^{
        if (syncTask == UIBackgroundTaskInvalid) return;
        const auto task = syncTask; syncTask = UIBackgroundTaskInvalid;
        [UIApplication.sharedApplication endBackgroundTask:task];
    };
    if (NSThread.isMainThread) end(); else dispatch_sync(dispatch_get_main_queue(), end);
}

void societySetSyncScreenActive(bool active) {
    dispatch_async(dispatch_get_main_queue(), ^{
        const bool previous = UIApplication.sharedApplication.idleTimerDisabled;
        UIApplication.sharedApplication.idleTimerDisabled = active;
        if (previous != UIApplication.sharedApplication.idleTimerDisabled
            && qEnvironmentVariableIntValue("SOCIETY_DISCOVERY_TRACE") == 1)
            qInfo("Society transfer screen: %s", UIApplication.sharedApplication.idleTimerDisabled ? "retained" : "released");
    });
}
