#pragma once
#include <Qt>

// Foreground transfer progress must remain visible until this batch finishes.
inline bool societySyncNeedsScreen(bool synchronizing, bool connected, Qt::ApplicationState state) {
    return synchronizing && connected && state == Qt::ApplicationActive;
}
void societySetSyncScreenActive(bool active);
