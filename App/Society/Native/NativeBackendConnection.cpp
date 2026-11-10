#include "NativeBackendConnection.h"
#include <algorithm>
#include <utility>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace society::native {
struct NativeBackendConnection::State {
    State(ii::runtime::EventLoop &loop, NativeBackendConnection *owner, PeerVerifier verifier)
        : socket(loop), owner(owner), verifier(std::move(verifier)) {}
    ii::network::LocalSocket socket;
    NativeBackendConnection *owner;
    PeerVerifier verifier;
    BackendFrames frames;
    bool authenticated = false;
};

NativeBackendConnection::NativeBackendConnection(ii::runtime::EventLoop &loop, PeerVerifier verifier)
    : m_state(std::make_shared<State>(loop, this, std::move(verifier)))
{
    if (!m_state->verifier) {
        m_state->verifier = [](const auto &credentials) {
#if defined(_WIN32)
            (void)credentials;
            return false;
#else
            return credentials && credentials->user && *credentials->user == static_cast<std::uint64_t>(geteuid());
#endif
        };
    }
    std::weak_ptr<State> weak = m_state;
    m_state->socket.connected = [weak] {
        auto state = weak.lock();
        if (!state || !state->owner) return;
        if (!state->verifier(state->socket.peer_credentials())) {
            auto callback = state->owner->onError;
            state->socket.close();
            if (callback) callback("The Society backend peer could not be verified.");
            return;
        }
        state->authenticated = true;
        auto callback = state->owner->onConnected;
        if (callback) callback();
    };
    m_state->socket.readable = [weak] {
        auto state = weak.lock();
        if (!state || !state->owner || !state->authenticated) return;
        while (state->socket.available_bytes()) {
            auto bytes = state->socket.read_some(std::min<std::size_t>(state->socket.available_bytes(), 65536));
            if (bytes.empty()) return;
            std::vector<ii::core::Value> messages;
            if (!state->frames.feed(bytes, messages)) {
                auto error = state->frames.error();
                auto callback = state->owner->onError;
                state->authenticated = false;
                state->socket.close();
                if (callback) callback(std::move(error));
                return;
            }
            for (auto &message : messages) {
                if (!state->owner || !state->authenticated) return;
                auto callback = state->owner->onMessage;
                if (callback) callback(std::move(message));
            }
        }
    };
    m_state->socket.disconnected = [weak] {
        auto state = weak.lock();
        if (!state || !state->owner) return;
        state->authenticated = false;
        auto callback = state->owner->onDisconnected;
        if (callback) callback();
    };
    m_state->socket.failed = [weak](std::string error) {
        auto state = weak.lock();
        if (!state || !state->owner) return;
        state->authenticated = false;
        auto callback = state->owner->onError;
        if (callback) callback(std::move(error));
    };
}

NativeBackendConnection::~NativeBackendConnection()
{
    m_state->owner = nullptr;
    m_state->socket.close();
}

std::expected<void, std::string> NativeBackendConnection::connect(const std::string &path)
{
#if defined(_WIN32)
    (void)path;
    return std::unexpected("The Society backend uses a Windows named pipe; its native adapter is not yet available.");
#else
    if (m_state->socket.state() != ii::network::SocketState::closed)
        return std::unexpected("The Society backend connection is already active.");
    m_state->frames.reset();
    ii::network::StreamOptions options;
    options.receive_bytes = BackendFrames::maximum_body + 4;
    options.pending_bytes = 2 * (BackendFrames::maximum_body + 4);
    auto result = m_state->socket.connect({path}, options);
    if (!result) return std::unexpected(result.error().message);
    return {};
#endif
}

bool NativeBackendConnection::send(const ii::core::Value &message)
{
    if (!m_state->authenticated) return false;
    auto frame = BackendFrames::encode(message);
    return frame && m_state->socket.write(*frame);
}

bool NativeBackendConnection::connected() const noexcept { return m_state->authenticated; }
void NativeBackendConnection::close()
{
    m_state->authenticated = false;
    m_state->socket.close();
}
}
