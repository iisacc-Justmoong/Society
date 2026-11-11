#include "NativeNavigationViewModel.h"

bool NativeNavigationViewModel::setCurrent(std::string_view page) {
    Action current;
    if (page == "Environment" || page == "Dashboard") current = Action::Environment;
    else if (page == "Tools") current = Action::Tasks;
    else if (page == "Storage") current = Action::Library;
    else if (page == "Space") current = Action::Space;
    else if (page == "Browse") current = Action::Browse;
    else if (page == "People") current = Action::People;
    else if (page == "Hosting") current = Action::Hosting;
    else return false;
    environmentActive.set(current == Action::Environment);
    tasksActive.set(current == Action::Tasks);
    libraryActive.set(current == Action::Library);
    spaceActive.set(current == Action::Space);
    browseActive.set(current == Action::Browse);
    peopleActive.set(current == Action::People);
    hostingActive.set(current == Action::Hosting);
    return true;
}

bool NativeNavigationViewModel::isSelected(Action action) const {
    switch (action) {
    case Action::Environment: return environmentActive.get();
    case Action::Tasks: return tasksActive.get();
    case Action::Library: return libraryActive.get();
    case Action::Space: return spaceActive.get();
    case Action::Browse: return browseActive.get();
    case Action::People: return peopleActive.get();
    case Action::Hosting: return hostingActive.get();
    }
    return false;
}

void NativeNavigationViewModel::request(Action action) { if (onNavigate) onNavigate(action); }
void NativeNavigationViewModel::environment() { request(Action::Environment); }
void NativeNavigationViewModel::tasks() { request(Action::Tasks); }
void NativeNavigationViewModel::library() { request(Action::Library); }
void NativeNavigationViewModel::space() { request(Action::Space); }
void NativeNavigationViewModel::browse() { request(Action::Browse); }
void NativeNavigationViewModel::people() { request(Action::People); }
void NativeNavigationViewModel::hosting() { request(Action::Hosting); }
