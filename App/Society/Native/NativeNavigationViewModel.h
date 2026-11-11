#pragma once

#include <iiSDKCore/Core/Property/Observable.h>
#include <LVUI/IconCatalog.h>
#include <functional>
#include <memory>
#include <string_view>

// The hosting shell owns navigation and confirms the active destination.
struct NativeNavigationViewModel final {
    enum class Action { Environment, Tasks, Library, Space, Browse, People, Hosting };

    ii::core::Observable<bool> environmentActive{true}, tasksActive{false}, libraryActive{false};
    ii::core::Observable<bool> spaceActive{false}, browseActive{false}, peopleActive{false}, hostingActive{false};
    std::shared_ptr<const lvui::IconSource> iconSource;
    std::function<void(Action)> onNavigate;

    bool setCurrent(std::string_view page);
    [[nodiscard]] bool isSelected(Action action) const;
    void environment();
    void tasks();
    void library();
    void space();
    void browse();
    void people();
    void hosting();
private:
    void request(Action action);
};
