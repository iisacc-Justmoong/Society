#pragma once

#include "NativeBackendWire.h"
#include <iiNetwork>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace society::native {

// The connection and its callbacks belong to the Core event-loop thread.
class NativeBackendConnection final {
public:
    using PeerVerifier = std::function<bool(const std::optional<ii::network::PeerCredentials> &)>;
    explicit NativeBackendConnection(ii::runtime::EventLoop &, PeerVerifier = {});
    ~NativeBackendConnection();
    NativeBackendConnection(const NativeBackendConnection &) = delete;
    NativeBackendConnection &operator=(const NativeBackendConnection &) = delete;

    [[nodiscard]] std::expected<void, std::string> connect(const std::string &path);
    [[nodiscard]] bool send(const ii::core::Value &message);
    [[nodiscard]] bool connected() const noexcept;
    void close();

    std::function<void()> onConnected, onDisconnected;
    std::function<void(ii::core::Value)> onMessage;
    std::function<void(std::string)> onError;

private:
    struct State;
    std::shared_ptr<State> m_state;
};
}
