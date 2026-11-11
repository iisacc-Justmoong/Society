#include "Society/Native/NativeNavigationViewModel.h"
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using Action = NativeNavigationViewModel::Action;
    NativeNavigationViewModel vm;
    std::vector<Action> requests;
    vm.onNavigate = [&](Action action) { requests.push_back(action); };
    vm.environment(); vm.tasks(); vm.library(); vm.space(); vm.browse(); vm.people(); vm.hosting();
    check(requests == std::vector<Action>{Action::Environment, Action::Tasks, Action::Library,
        Action::Space, Action::Browse, Action::People, Action::Hosting}, "each Figma segment must emit its own destination");

    const std::array<std::pair<const char *, Action>, 7> states{{
        {"Environment", Action::Environment}, {"Tools", Action::Tasks}, {"Storage", Action::Library},
        {"Space", Action::Space}, {"Browse", Action::Browse}, {"People", Action::People},
        {"Hosting", Action::Hosting}}};
    for (const auto &[page, selected] : states) {
        check(vm.setCurrent(page), "known destination accepted");
        for (const auto &[unused, candidate] : states) {
            (void)unused;
            check(vm.isSelected(candidate) == (candidate == selected), "one selected segment");
        }
    }
    check(vm.setCurrent("Dashboard") && vm.isSelected(Action::Environment), "workspace is within Environment");
    check(!vm.setCurrent("unknown") && vm.isSelected(Action::Environment), "unknown destination preserves selection");
    vm.onNavigate = {};
    vm.people();
    check(vm.isSelected(Action::Environment), "unhandled requests cannot change selection");
}
