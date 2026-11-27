#pragma once
#include "NativeFilesViewModel.h"
#include <LVUI/Popover.h>
#include <iiGUI>
#include <algorithm>

namespace society::native {
// Position the shared LVUI notification at the upper right of the Files window.
inline void presentFilesNotification(ii::gui::Widget popup,double width,const NativeFilesViewModel &model) {
    if (model.notificationMessage.get().empty()) {
        (void)popup.close_popup();
        return;
    }
    const auto margin=lvui::PopoverConfiguration{}.margins;
    (void)popup.open_at(std::max(0.0,width-model.notificationWidth-margin),margin);
}
}
