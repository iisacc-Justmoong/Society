#include "Society/Notifications/NotificationStore.h"
#include <stdexcept>
#include <string>

static void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    society::notifications::NotificationStore store;
    store.record("Models", {"one", "two", "one"});
    check(store.entries().size()==2 && store.entries()[0].message=="one", "deduplicate one source and batch");
    check(store.dismiss(0) && store.entries()[0].message=="two", "dismiss only the chosen entry");
    store.record("Models", {"one", "two"});
    check(store.entries().size()==1, "dismissed active warnings stay dismissed");
    store.record("Models", {});
    store.record("Models", {"one", "two"});
    check(store.entries().size()==2 && store.entries()[1].message=="one", "a cleared warning can recur without duplicating a visible warning");
    store.record("Storage", {"one"});
    check(store.entries().size()==3 && store.entries()[2].source=="Storage", "different sources remain independent");
    check(!store.dismiss(100), "an invalid dismissal is ignored");
    store.clear();
    store.record("Models", {"one", "two"});
    store.record("Storage", {"one"});
    check(store.entries().empty(), "clear retains each source's active state");
    store.record("Models", {});
    store.record("Models", {"one"});
    check(store.entries().size()==1, "a warning can return after clearing its source");
}
