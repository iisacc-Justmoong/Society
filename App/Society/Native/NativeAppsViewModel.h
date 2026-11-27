#pragma once

#include <LVUI>
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// One observed catalogue item. The host owns discovery, installation and image
/// decoding; this value carries no authority to open files or start processes.
struct NativeAppsItem final {
    std::string id, name, description, status, action, publisher;
    bool installed = false, available = false;
    std::shared_ptr<const ii::gui::RasterImage> icon;
    bool selected = false;
    friend bool operator==(const NativeAppsItem &, const NativeAppsItem &) = default;
};

/// A layout row is a value snapshot, not an owner of the underlying catalogue.
struct NativeAppsRow final {
    std::string id;
    std::vector<NativeAppsItem> items;
    friend bool operator==(const NativeAppsRow &, const NativeAppsRow &) = default;
};

/// Presentation state for the Figma Apps view. Call and mutate on the UI
/// thread only. The host supplies catalogue snapshots and serializes actions;
/// callbacks borrow no data beyond the call and never imply installation success.
struct NativeAppsViewModel final {
    ii::core::Observable<std::vector<NativeAppsItem>> visibleApps;
    ii::core::Observable<std::vector<NativeAppsRow>> rows;
    ii::core::Observable<std::string> selectedId, detailName, detailDescription, detailStatus, detailAction, detailPublisher, detailLocation;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> detailIcon;
    ii::core::Observable<std::string> query, heading{"All apps"}, count, activity{"No active downloads"};
    ii::core::Observable<std::string> subtitle{"Manage creative apps on this device."}, searchPlaceholder{"Search apps"};
    ii::core::Observable<std::string> notice;
    ii::core::Observable<bool> allActive{true}, myActive{false}, webActive{false}, extensionActive{false}, elementsActive{false};
    ii::core::Observable<bool> collectionAvailable{true}, hasSelection{false}, hasApps{false}, actionEnabled{false}, busy{false}, showDetail{true}, compact{false}, showDevice{true}, nativeCollection{true}, hasNotice{false};
    ii::core::Observable<double> viewportWidth{1440}, viewportHeight{844}, contentWidth{1172}, catalogWidth{748}, detailWidth{400}, listHeight{420}, cardWidth{228}, searchWidth{300};
    std::shared_ptr<const lvui::IconSource> iconSource;
    /// Called only for an enabled selected item. The host validates and executes
    /// the action; ownership, lifetime, errors and completion remain external.
    std::function<void(const std::string &)> onAction;

    /// Replace observations atomically, retaining a selection only while visible.
    void setApps(std::vector<NativeAppsItem> apps);
    /// Supply the independent browser shortcut provider; no URL is opened here.
    void setWebApps(std::vector<NativeAppsItem> apps, bool connected);
    /// Change the search query; matching folds ASCII in name, description and publisher.
    void searchEdited(const std::string &value);
    /// Select the available local-app snapshot without requesting external work.
    void showAllApps();
    /// Select observed local installations, retaining the current search query.
    void showMyApps();
    /// Select browser shortcuts or an explicit disconnected state.
    void showWebApps();
    /// Select the extension collection, whose provider may be disconnected.
    void showExtensions();
    /// Select the elements collection, whose provider may be disconnected.
    void showElements();
    /// Produce a row action keyed by a stable product identifier.
    std::function<void()> selectApp(const std::string &id);
    /// Select a visible item and request its action, using the same guard as detail.
    std::function<void()> actionFor(const std::string &id);
    /// Request the selected action only when the host has marked it available.
    void performAction();
    /// Reflect an externally supplied operation state without inferring success.
    void setActivity(const std::string &description, bool inProgress);
    /// Show an operation failure without impersonating an installation event.
    void setNotice(const std::string &description);
    /// Reflow the catalogue for the supplied logical viewport size.
    void resize(double width, double height);
private:
    enum class Collection { All, Mine, Web, Extensions, Elements };
    std::vector<NativeAppsItem> apps_;
    std::vector<NativeAppsItem> webApps_;
    bool webConnected_ = false;
    Collection collection_ = Collection::All;
    /// Update collection flags and recompute presentation values synchronously.
    void selectCollection(Collection collection);
    /// Reconcile visible rows and selection from owned value snapshots only.
    void rebuild();
};
