#pragma once

namespace society::daemon {
enum class RegistrationOperation { Status, Register, Unregister };

template<class State> struct RegistrationOutcome {
    State state;
    bool ok;
};

template<class State, class ReadState, class ChangeState>
RegistrationOutcome<State> registrationOutcome(RegistrationOperation operation,
    State enabled, State unregistered, ReadState read, ChangeState change)
{
    auto state = read();
    const bool needed = (operation == RegistrationOperation::Register && state != enabled)
        || (operation == RegistrationOperation::Unregister && state != unregistered);
    bool ok = true;
    if (needed) {
        ok = change(operation);
        state = read();
    }
    if (operation == RegistrationOperation::Register && state != enabled) ok = false;
    return {state, ok};
}
} // namespace society::daemon
