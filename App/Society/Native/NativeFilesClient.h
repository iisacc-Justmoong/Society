#pragma once
#include "NativeBackendConnection.h"
#include "NativeFilesViewModel.h"

namespace society::native {
// The connection and model outlive this exclusive binding on the event-loop thread.
class NativeFilesClient final {
public:
    NativeFilesClient(NativeBackendConnection &connection, NativeFilesViewModel &model)
        : m_connection(connection), m_model(model) {
        m_model.onRequest = [&connection](ii::core::Value message) { return connection.send(message); };
        m_connection.onMessage = [&model](ii::core::Value message) { model.receive(message); };
        m_connection.onDisconnected = [&model] { model.disconnected(); };
        m_connection.onError = [&model](std::string error) { model.backendError(std::move(error)); };
    }
    ~NativeFilesClient() {
        m_model.onRequest = {};
        m_connection.onMessage = {}; m_connection.onDisconnected = {}; m_connection.onError = {};
    }
    NativeFilesClient(const NativeFilesClient &) = delete;
    NativeFilesClient &operator=(const NativeFilesClient &) = delete;
private:
    NativeBackendConnection &m_connection;
    NativeFilesViewModel &m_model;
};
}
