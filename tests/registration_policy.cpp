#include "Daemon/ServiceRegistrationPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
enum class State { Enabled, NotRegistered, RequiresApproval, NotFound };
using Operation = society::daemon::RegistrationOperation;
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    constexpr std::array states{State::Enabled, State::NotRegistered,
                               State::RequiresApproval, State::NotFound};
    int cases = 0;
    for (const auto operation : {Operation::Status, Operation::Register, Operation::Unregister}) {
        for (const auto initial : states) for (const auto after : states) for (const bool actionOk : {false, true}) {
            auto state = initial;
            int reads = 0, changes = 0;
            const bool needed = (operation == Operation::Register && initial != State::Enabled)
                || (operation == Operation::Unregister && initial != State::NotRegistered);
            const auto reply = society::daemon::registrationOutcome(operation, State::Enabled,
                State::NotRegistered, [&] { ++reads; return state; }, [&](Operation action) {
                    require(action == operation, "wrong native service mutation");
                    ++changes; state = after; return actionOk;
                });
            const auto expectedState = needed ? after : initial;
            const bool expectedOk = (!needed || actionOk)
                && (operation != Operation::Register || expectedState == State::Enabled);
            require(reply.state == expectedState && reply.ok == expectedOk, "native reply changed");
            require(changes == int(needed), "redundant or missing native mutation");
            if (reads != (needed ? 2 : 1)) {
                std::cerr << "reads=" << reads << " expected=" << (needed ? 2 : 1) << '\n';
                throw std::runtime_error("redundant synchronous native service status query");
            }
            ++cases;
        }
    }
    std::cout << cases << " native registration outcomes retain results with bounded status queries\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
