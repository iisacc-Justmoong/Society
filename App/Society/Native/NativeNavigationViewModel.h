#pragma once

#include <iiSDKCore>
#include <LVUI>
#include <functional>
#include <memory>
#include <string_view>

/// Describes the nine reference destinations; the shell confirms their active state.
struct NativeNavigationViewModel final {
    /// Identifies the destination requested by an LVUI segment.
    enum class Action { Environment, Tasks, Library, Space, Apps, Service, Community, Learning, Hosting };

    ii::core::Observable<bool> environmentActive{true}, tasksActive{false}, libraryActive{false};
    ii::core::Observable<bool> spaceActive{false}, appsActive{false}, serviceActive{false};
    ii::core::Observable<bool> communityActive{false}, learningActive{false}, hostingActive{false};
    std::shared_ptr<const lvui::IconSource> iconSource;
    std::function<void(Action)> onNavigate;

    /// Synchronize the checked segment to a shell page; unknown pages leave it unchanged.
    bool setCurrent(std::string_view page);
    /// Return whether the shell has confirmed this destination as active.
    [[nodiscard]] bool isSelected(Action action) const;
    /// Request the Workspace destination represented by Environment.
    void environment();
    /// Request the existing tools destination.
    void tasks();
    /// Request the existing storage destination.
    void library();
    /// Request the native Space destination.
    void space();
    /// Request the existing apps catalogue.
    void apps();
    /// Request the Service destination.
    void service();
    /// Request the Community destination.
    void community();
    /// Request the Learning destination.
    void learning();
    /// Request the existing hosting information.
    void hosting();
private:
    /// Forward an action without preemptively changing the shell's selection.
    void request(Action action);
};
