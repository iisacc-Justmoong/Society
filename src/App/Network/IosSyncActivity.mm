#include "MobileSyncActivity.h"
#include <TaskActivityBridge.h>
#include <QDebug>
#include <QElapsedTimer>
#include <memory>
#include <utility>
#import <BackgroundTasks/BackgroundTasks.h>
#import <UIKit/UIKit.h>

namespace { UIBackgroundTaskIdentifier syncTask = UIBackgroundTaskInvalid; }

namespace {
struct ContinuedSync {
    NSString *identifier = nil;
    BGTask *task = nil;
    std::function<void()> expired;
    qint64 completed = 0, total = 1;
    QElapsedTimer elapsed;
    qint64 lastPublished = -250, lastLogged = 0;
};
std::shared_ptr<ContinuedSync> continuedSync;
}

bool societyBeginContinuedSync(std::function<void()> expired) {
    // The foreground app-opening catch-up or Sync now action owns this finite
    // batch. Discovery timers cannot create another continued-processing task.
    if (UIApplication.sharedApplication.applicationState == UIApplicationStateBackground) return false;
    static const bool restored = [] { society_activity_restore(); return true; }();
    Q_UNUSED(restored);
    society_activity_begin("society-sync", "Society", "arrow.triangle.2.circlepath", "Comparing files…");
    if (@available(iOS 26.0, *)) {
        societyCompleteContinuedSync(false);
        auto current = std::make_shared<ContinuedSync>();
        current->elapsed.start();
        current->identifier = [@"com.iisacc.society.sync." stringByAppendingString:NSUUID.UUID.UUIDString];
        current->expired = std::move(expired);
        continuedSync = current;
        const std::weak_ptr<ContinuedSync> weak = current;
        // Cover handoff and the older/denied-task fallback with a finite grant.
        const bool fallback = societyBeginBackgroundSync([weak] {
            if (const auto self = weak.lock(); self && !self->task && self->expired) self->expired();
        });
        NSString *identifier = current->identifier;
        const BOOL registered = [BGTaskScheduler.sharedScheduler registerForTaskWithIdentifier:identifier
            usingQueue:dispatch_get_main_queue() launchHandler:^(__kindof BGTask *task) {
                const auto self = weak.lock();
                if (!self || continuedSync != self) { [task setTaskCompletedWithSuccess:NO]; return; }
                self->task = task;
                task.expirationHandler = ^{
                    dispatch_async(dispatch_get_main_queue(), ^{
                        if (const auto active = weak.lock(); active && continuedSync == active && active->expired)
                        {
                            qInfo("Society: background execution expired after %lld ms", active->elapsed.elapsed());
                            active->expired();
                        }
                    });
                };
                societyUpdateContinuedSync(self->completed, self->total);
                societyEndBackgroundSync();
                qInfo("Society: continued background synchronization granted");
            }];
        if (!registered) {
            if (!fallback) societyCompleteContinuedSync(false);
            return fallback;
        }
        qInfo("Society: requesting continued background synchronization");
        auto *request = [[BGContinuedProcessingTaskRequest alloc] initWithIdentifier:identifier
            title:NSLocalizedString(@"Syncing Society", nil) subtitle:NSLocalizedString(@"Comparing files…", nil)];
        request.requiredResources = BGContinuedProcessingTaskRequestResourcesDefault;
        request.strategy = BGContinuedProcessingTaskRequestSubmissionStrategyFail;
        const auto submitted = ^(NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                const auto self = weak.lock();
                if (!self || continuedSync != self) {
                    [BGTaskScheduler.sharedScheduler cancelTaskRequestWithIdentifier:identifier]; return;
                }
                if (error) {
                    qWarning() << "Society background sync:" << QString::fromNSString(error.localizedDescription);
                    if (syncTask == UIBackgroundTaskInvalid && self->expired) self->expired();
                }
            });
        };
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            if (@available(iOS 27.0, *)) {
                [BGTaskScheduler.sharedScheduler submitTaskRequest:request completionHandler:submitted];
            } else {
                NSError *error = nil;
                [BGTaskScheduler.sharedScheduler submitTaskRequest:request error:&error];
                submitted(error);
            }
        });
        return true;
    }
    return societyBeginBackgroundSync(std::move(expired));
}

void societyUpdateContinuedSync(qint64 completed, qint64 total) {
    NSString *detail = completed > 0
        ? [NSString stringWithFormat:NSLocalizedString(@"%@ processed", nil),
            [NSByteCountFormatter stringFromByteCount:completed countStyle:NSByteCountFormatterCountStyleFile]]
        : NSLocalizedString(@"Comparing files…", nil);
    society_activity_update("society-sync", detail.UTF8String, completed, total, "running");
    if (!continuedSync) return;
    continuedSync->completed = completed; continuedSync->total = total;
    if (@available(iOS 26.0, *)) {
        if (auto *task = (BGContinuedProcessingTask *)continuedSync->task) {
            const auto elapsed = continuedSync->elapsed.elapsed();
            if (elapsed - continuedSync->lastPublished < 250) return;
            continuedSync->lastPublished = elapsed;
            task.progress.totalUnitCount = total;
            task.progress.completedUnitCount = completed;
            if (qEnvironmentVariableIntValue("SOCIETY_DISCOVERY_TRACE") == 1
                && elapsed - continuedSync->lastLogged >= 5000) {
                continuedSync->lastLogged = elapsed;
                qInfo("Society: sync progress elapsed=%lld completed=%lld total=%lld background=%d",
                    elapsed, completed, total, UIApplication.sharedApplication.applicationState == UIApplicationStateBackground);
            }
            NSString *subtitle = completed > 0
                ? [NSString stringWithFormat:NSLocalizedString(@"%@ processed", nil),
                    [NSByteCountFormatter stringFromByteCount:completed countStyle:NSByteCountFormatterCountStyleFile]]
                : NSLocalizedString(@"Comparing files…", nil);
            [task updateTitle:NSLocalizedString(@"Syncing Society", nil) subtitle:subtitle];
        }
    }
}

void societyCompleteContinuedSync(bool success) {
    society_activity_finish("society-sync", success ? "completed" : "paused");
    const auto self = std::exchange(continuedSync, {});
    if (self) {
        qInfo() << "Society: continued synchronization ended; success:" << success;
        self->expired = {};
        if (self->task) {
            if (@available(iOS 26.0, *)) {
                auto *task = (BGContinuedProcessingTask *)self->task;
                if (success) task.progress.completedUnitCount = task.progress.totalUnitCount;
            }
            self->task.expirationHandler = nil;
            [self->task setTaskCompletedWithSuccess:success];
            self->task = nil;
        }
        [BGTaskScheduler.sharedScheduler cancelTaskRequestWithIdentifier:self->identifier];
    }
    societyEndBackgroundSync();
}
void societyRestartSyncPresentation() { society_activity_allow_restart("society-sync"); }
bool societyBeginBackgroundSync(std::function<void()> expired) {
    __block bool granted = false;
    const auto begin = ^{
        if (syncTask == UIBackgroundTaskInvalid)
            syncTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Society container synchronization"
                expirationHandler:^{
                    const auto task = syncTask;
                    expired();
                    if (task != UIBackgroundTaskInvalid && syncTask == task) {
                        syncTask = UIBackgroundTaskInvalid;
                        [UIApplication.sharedApplication endBackgroundTask:task];
                    }
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
