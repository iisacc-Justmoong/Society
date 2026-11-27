#include "NativeAppsViewModel.h"

#include <algorithm>
#include <utility>

namespace {
// The catalogue currently supplies English product names. Preserve UTF-8 bytes
// and fold ASCII only; this does not claim locale-aware Unicode matching.
std::string folded(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    return text;
}
}

void NativeAppsViewModel::setApps(std::vector<NativeAppsItem> apps) {
    apps_ = std::move(apps);
    rebuild();
}
void NativeAppsViewModel::setWebApps(std::vector<NativeAppsItem> apps, bool connected) {
    webApps_ = std::move(apps); webConnected_ = connected;
    if (collection_ == Collection::Web) { collectionAvailable.set(connected); rebuild(); }
}
void NativeAppsViewModel::searchEdited(const std::string &value) { query.set(value); rebuild(); }
void NativeAppsViewModel::showAllApps() { selectCollection(Collection::All); }
void NativeAppsViewModel::showMyApps() { selectCollection(Collection::Mine); }
void NativeAppsViewModel::showWebApps() { selectCollection(Collection::Web); }
void NativeAppsViewModel::showExtensions() { selectCollection(Collection::Extensions); }
void NativeAppsViewModel::showElements() { selectCollection(Collection::Elements); }
void NativeAppsViewModel::selectCollection(Collection collection) {
    collection_ = collection;
    notice.set(""); hasNotice.set(false);
    allActive.set(collection == Collection::All); myActive.set(collection == Collection::Mine);
    webActive.set(collection == Collection::Web); extensionActive.set(collection == Collection::Extensions);
    elementsActive.set(collection == Collection::Elements);
    const char *name = collection == Collection::All ? "All apps" : collection == Collection::Mine ? "My apps"
        : collection == Collection::Web ? "Web apps" : collection == Collection::Extensions ? "Extension" : "Elements";
    heading.set(name);
    nativeCollection.set(collection == Collection::All || collection == Collection::Mine);
    subtitle.set(collection == Collection::All ? "Manage creative apps on this device." : collection == Collection::Mine ? "Apps installed on this device."
        : collection == Collection::Web ? "Open web apps available to your account." : collection == Collection::Extensions ? "Browse extensions." : "Browse elements.");
    searchPlaceholder.set(collection == Collection::Extensions ? "Search extensions" : collection == Collection::Elements ? "Search elements" : "Search apps");
    collectionAvailable.set(collection == Collection::All || collection == Collection::Mine || (collection == Collection::Web && webConnected_));
    resize(viewportWidth.get(), viewportHeight.get());
}
std::function<void()> NativeAppsViewModel::selectApp(const std::string &id) {
    return [this, id] {
        const auto &items = visibleApps.get();
        if (std::any_of(items.begin(), items.end(), [&](const auto &app) { return app.id == id; })) {
            selectedId.set(id);
            rebuild();
        }
    };
}
std::function<void()> NativeAppsViewModel::actionFor(const std::string &id) {
    return [this, id] { selectApp(id)(); if (selectedId.get() == id) performAction(); };
}
void NativeAppsViewModel::performAction() {
    if (actionEnabled.get() && onAction) onAction(selectedId.get());
}
void NativeAppsViewModel::setActivity(const std::string &description, bool inProgress) {
    activity.set(description); busy.set(inProgress); rebuild();
}
void NativeAppsViewModel::setNotice(const std::string &description) { notice.set(description); hasNotice.set(!description.empty()); }
void NativeAppsViewModel::resize(double width, double height) {
    viewportWidth.set(std::max(0.0, width)); viewportHeight.set(std::max(0.0, height));
    compact.set(width < 700.0);
    const double content = std::max(0.0, width - (compact.get() ? 0.0 : 220.0) - 48.0);
    contentWidth.set(content);
    const bool detail = nativeCollection.get() && content >= 760.0;
    showDetail.set(detail);
    detailWidth.set(400.0);
    catalogWidth.set(std::max(0.0, content - (detail ? 424.0 : 0.0)));
    const auto columns = nativeCollection.get()
        ? std::clamp(static_cast<int>((catalogWidth.get() - 16.0) / 244.0), 1, 3)
        : std::clamp(static_cast<int>((catalogWidth.get() - 16.0) / 385.0), 1, 3);
    cardWidth.set(std::max(0.0, nativeCollection.get() ? std::min(228.0, catalogWidth.get() - 32.0)
        : (catalogWidth.get() - 32.0 - (columns - 1) * 16.0) / columns));
    showDevice.set(nativeCollection.get() && catalogWidth.get() >= 480.0);
    searchWidth.set(std::max(0.0, std::min(300.0, catalogWidth.get() - (showDevice.get() ? 180.0 : 0.0))));
    rebuild();
}
void NativeAppsViewModel::rebuild() {
    std::vector<NativeAppsItem> visible;
    const auto raw = query.get();
    const auto first = raw.find_first_not_of(" \t\r\n");
    const auto last = raw.find_last_not_of(" \t\r\n");
    const auto needle = first == std::string::npos ? std::string{} : folded(raw.substr(first, last - first + 1));
    if (collectionAvailable.get()) {
        const auto &source = collection_ == Collection::Web ? webApps_ : apps_;
        for (const auto &app : source) {
            if (collection_ == Collection::Mine && !app.installed) continue;
            if (!needle.empty() && folded(app.name + " " + app.description + " " + app.publisher).find(needle) == std::string::npos) continue;
            visible.push_back(app);
        }
    }
    auto selected = selectedId.get();
    hasApps.set(!visible.empty());
    if (std::none_of(visible.begin(), visible.end(), [&](const auto &app) { return app.id == selected; }))
        selected = visible.empty() ? std::string{} : visible.front().id;
    selectedId.set(selected);
    for (auto &app : visible) app.selected = app.id == selected;
    const auto found = std::find_if(visible.begin(), visible.end(), [&](const auto &app) { return app.id == selected; });
    const bool selectedPresent = found != visible.end();
    hasSelection.set(selectedPresent);
    detailName.set(selectedPresent ? found->name : ""); detailDescription.set(selectedPresent ? found->description : "");
    detailStatus.set(selectedPresent ? found->status : ""); detailAction.set(selectedPresent ? found->action : "");
    detailPublisher.set(selectedPresent ? found->publisher : "");
    detailLocation.set(selectedPresent ? (collection_ == Collection::Web ? "Web browser" : found->installed ? "This device" : "Not installed") : "");
    detailIcon.set(selectedPresent ? found->icon : std::shared_ptr<const ii::gui::RasterImage>{});
    actionEnabled.set(selectedPresent && found->available && !busy.get());
    count.set(heading.get() + " · " + std::to_string(visible.size()));
    const auto columns = nativeCollection.get()
        ? std::clamp(static_cast<int>((catalogWidth.get() - 16.0) / 244.0), 1, 3)
        : std::clamp(static_cast<int>((catalogWidth.get() - 16.0) / 385.0), 1, 3);
    std::vector<NativeAppsRow> grouped;
    for (std::size_t i = 0; i < visible.size(); i += static_cast<std::size_t>(columns)) {
        NativeAppsRow row{visible[i].id, {}};
        for (std::size_t j = i; j < visible.size() && j < i + static_cast<std::size_t>(columns); ++j) row.items.push_back(visible[j]);
        grouped.push_back(std::move(row));
    }
    const double rowHeight = nativeCollection.get() ? 200.0 : 168.0;
    const double desired = grouped.empty() ? 100.0 : grouped.size() * (rowHeight + 16.0) - 16.0;
    const double available = std::max(100.0, viewportHeight.get() - (compact.get() ? 420.0 : 380.0));
    listHeight.set(std::min(available, desired));
    visibleApps.set(std::move(visible)); rows.set(std::move(grouped));
}
