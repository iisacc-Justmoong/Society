#include "Society/Native/NativeAppsViewModel.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    NativeAppsViewModel vm;
    vm.setApps({{"congregation", "Congregation", "Canvas", "Not installed", "Install", "iisacc", false, true},
                {"dreamscapes", "Dreamscapes", "Images", "Installed", "Open", "iisacc", true, true},
                {"unavailable", "Unavailable", "", "Not installed", "Install", "iisacc", false, false}});
    check(vm.visibleApps.get().size() == 3 && vm.selectedId.get() == "congregation", "All apps presents the supplied snapshot and selects the first item");
    check(vm.listHeight.get() == 200, "a single card row does not reserve an empty scrolling region");
    check(vm.detailName.get() == "Congregation" && vm.detailAction.get() == "Install", "detail follows selection");
    vm.selectApp("dreamscapes")();
    check(vm.detailName.get() == "Dreamscapes" && vm.detailAction.get() == "Open", "selecting a card updates detail");
    vm.searchEdited("  CANVAS  ");
    check(vm.visibleApps.get().size() == 1 && vm.visibleApps.get()[0].id == "congregation", "search includes catalogue descriptions");
    vm.searchEdited("CONG");
    check(vm.visibleApps.get().size() == 1 && vm.visibleApps.get()[0].id == "congregation", "search is case insensitive and scoped to the current collection");
    vm.showMyApps();
    check(vm.visibleApps.get().empty() && vm.selectedId.get().empty(), "a filtered selection cannot remain actionable");
    vm.searchEdited("");
    check(vm.visibleApps.get().size() == 1 && vm.selectedId.get() == "dreamscapes", "My apps contains observed installations only");
    std::vector<std::string> actions;
    vm.onAction = [&](const std::string &id) { actions.push_back(id); };
    vm.performAction();
    check(actions == std::vector<std::string>{"dreamscapes"}, "the host receives the selected product identity");
    vm.showAllApps(); vm.selectApp("unavailable")(); vm.performAction();
    check(actions.size() == 1, "an unavailable action is never dispatched");
    vm.setActivity("Downloading", true);
    check(vm.activity.get() == "Downloading" && vm.busy.get() && !vm.actionEnabled.get(), "busy work is visible and blocks repeated actions");
    vm.setActivity("Installer opened; awaiting completion", false);
    check(!vm.busy.get() && vm.activity.get().find("awaiting") != std::string::npos, "handoff remains distinct from completed installation");
    vm.showWebApps();
    check(vm.visibleApps.get().empty() && !vm.collectionAvailable.get(), "missing collection providers have an explicit empty state");
    check(!vm.nativeCollection.get() && !vm.showDetail.get() && vm.catalogWidth.get() == vm.contentWidth.get(), "Web apps uses the full-width Figma catalogue");
    vm.setWebApps({{"web:account", "Account", "Account service", "Web app", "Open web", "iisacc", false, true}}, true);
    check(vm.collectionAvailable.get() && vm.visibleApps.get().size() == 1 && vm.selectedId.get() == "web:account", "a connected browser shortcut provider supplies Web apps");
    vm.performAction();
    check(actions.back() == "web:account", "web shortcuts retain their separate identity");
    vm.showAllApps();
    vm.setApps({{"dreamscapes", "Dreamscapes", "Images", "Installed", "Open", "iisacc", true, true}});
    check(vm.selectedId.get() == "dreamscapes", "removing a selected product chooses a remaining visible product");
    vm.resize(480, 720);
    check(vm.compact.get() && !vm.showDetail.get() && vm.searchWidth.get() <= vm.catalogWidth.get() &&
          vm.cardWidth.get() <= vm.catalogWidth.get(), "narrow layouts keep navigation and controls within the content width");
}
