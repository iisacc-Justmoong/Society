#pragma once

#include <iiSDKCore>
#include <functional>
#include <string>

// Owns the notice content and its open state independently of the shell.
struct NativeNoticeViewModel final {
    ii::core::Observable<std::string> title{""}, message{""}, primaryText{""};
    ii::core::Observable<bool> open{false};
    std::function<void()> onDismiss;

    // Close the current presentation before notifying its host.
    void dismiss() {
        open.set(false);
        if (onDismiss) onDismiss();
    }
};
