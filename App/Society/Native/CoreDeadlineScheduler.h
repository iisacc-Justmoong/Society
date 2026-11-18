#pragma once
#include <iiAccountManager>
class QObject;
namespace society::native {
// Qt supplies only the one-shot wakeup. The explicitly requested work and
// cancellation remain in the iiSDKCore event loop; no idle loop is driven.
[[nodiscard]] iisacc::accounts::presentation::ScheduleCooldown coreDeadlineScheduler(QObject* owner);
}
