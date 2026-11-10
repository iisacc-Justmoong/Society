#pragma once
#include <QString>
#include <memory>

namespace iiMultimodalAgent::mcp { class LocalApplicationServer; }

// Owned by the daemon's application thread, independent of any window.
class DaemonMcp final {
public:
    explicit DaemonMcp(QString brokerDirectory, QString registrationDirectory = {});
    ~DaemonMcp();
    DaemonMcp(const DaemonMcp &) = delete;
    DaemonMcp &operator=(const DaemonMcp &) = delete;
    bool start();
    void stop();
    [[nodiscard]] QString errorString() const { return m_error; }
private:
    QString m_brokerDirectory, m_registrationDirectory, m_error;
    std::unique_ptr<iiMultimodalAgent::mcp::LocalApplicationServer> m_server;
};
