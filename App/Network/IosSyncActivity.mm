#include "MobileSyncActivity.h"
#include <QDebug>
#import <UIKit/UIKit.h>

void societySetSyncScreenActive(bool active) {
    dispatch_async(dispatch_get_main_queue(), ^{
        const bool previous = UIApplication.sharedApplication.idleTimerDisabled;
        UIApplication.sharedApplication.idleTimerDisabled = active;
        if (previous != UIApplication.sharedApplication.idleTimerDisabled
            && qEnvironmentVariableIntValue("SOCIETY_DISCOVERY_TRACE") == 1)
            qInfo("Society transfer screen: %s", UIApplication.sharedApplication.idleTimerDisabled ? "retained" : "released");
    });
}
