#pragma once
#include <LVUI/IconCatalog.h>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <string>

// Provides the LVUI presentation with its text and dismissal command.
struct NativeErrorNotificationViewModel final {
    ii::core::Observable<std::string> message{""};
    // The host derives these dimensions from the content viewport and iiGUI's
    // wrapped-text measurement; the LVUI view owns the resulting layout.
    ii::core::Observable<double> popupWidth{360};
    ii::core::Observable<double> popupHeight{0};
    ii::core::Observable<double> messageHeight{0};
    // Reuse the application's existing LVUI icon catalogue for dismissal.
    std::shared_ptr<const lvui::IconSource> iconSource;
    std::function<void()> onDismiss;
    // Request removal of the visible presentation, leaving the source intact.
    void dismiss() { if (onDismiss) onDismiss(); }
};
