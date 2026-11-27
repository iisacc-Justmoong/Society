#include "NativeNavigationViewModel.h"

bool NativeNavigationViewModel::setCurrent(std::string_view page) {
    Action current;
    if (page == "Environment" || page == "Dashboard" || page == "Browse") current = Action::Environment;
    else if (page == "Tools") current = Action::Tasks;
    else if (page == "Storage") current = Action::Library;
    else if (page == "Space") current = Action::Space;
    else if (page == "Apps") current = Action::Apps;
    else if (page == "Service") current = Action::Service;
    else if (page == "Community") current = Action::Community;
    else if (page == "Learning") current = Action::Learning;
    else if (page == "Hosting") current = Action::Hosting;
    else return false;
    environmentActive.set(current == Action::Environment);
    tasksActive.set(current == Action::Tasks);
    libraryActive.set(current == Action::Library);
    spaceActive.set(current == Action::Space);
    appsActive.set(current == Action::Apps);
    serviceActive.set(current == Action::Service);
    communityActive.set(current == Action::Community);
    learningActive.set(current == Action::Learning);
    hostingActive.set(current == Action::Hosting);
    return true;
}

bool NativeNavigationViewModel::isSelected(Action action) const {
    switch (action) {
    case Action::Environment: return environmentActive.get();
    case Action::Tasks: return tasksActive.get();
    case Action::Library: return libraryActive.get();
    case Action::Space: return spaceActive.get();
    case Action::Apps: return appsActive.get();
    case Action::Service: return serviceActive.get();
    case Action::Community: return communityActive.get();
    case Action::Learning: return learningActive.get();
    case Action::Hosting: return hostingActive.get();
    }
    return false;
}

void NativeNavigationViewModel::request(Action action) { if (onNavigate) onNavigate(action); }
void NativeNavigationViewModel::environment() { request(Action::Environment); }
void NativeNavigationViewModel::tasks() { request(Action::Tasks); }
void NativeNavigationViewModel::library() { request(Action::Library); }
void NativeNavigationViewModel::space() { request(Action::Space); }
void NativeNavigationViewModel::apps() { request(Action::Apps); }
void NativeNavigationViewModel::service() { request(Action::Service); }
void NativeNavigationViewModel::community() { request(Action::Community); }
void NativeNavigationViewModel::learning() { request(Action::Learning); }
void NativeNavigationViewModel::hosting() { request(Action::Hosting); }
