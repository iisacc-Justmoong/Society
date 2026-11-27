#include "Society/Native/ErrorNotificationQueue.h"
#include <cstdlib>

static void require(bool value) { if (!value) std::abort(); }
int main() {
    society::native::ErrorNotificationQueue queue;
    queue.push("");
    require(queue.empty());
    queue.push("first failure");
    queue.push("second failure");
    queue.push("second failure");
    require(queue.current() == "first failure");
    queue.dismiss();
    require(queue.current() == "second failure");
    queue.dismiss();
    require(queue.current() == "second failure");
    queue.dismiss();
    require(queue.empty());
    queue.dismiss();
    require(queue.empty());
}
