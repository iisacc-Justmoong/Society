#include "CTestSupport.h"
#include "QtEventWait.h"
#include <QCoreApplication>
#include <QEvent>
#include <QScopeGuard>
#include "Daemon/ResidentGateway.h"
#include <QCoreApplication>
#include <QHash>

class ResidentGatewayTest final : public QObject {
public:
    void independentRequestsAndDriveChange() {
        QHash<QString, ResidentGateway::Callback> callbacks;
        QStringList cancelled;
        QList<QPair<QString, QJsonObject>> received;
        ResidentGateway gateway;
        const auto connection = QObject::connect(&gateway, &ResidentGateway::event, &gateway,
            [&](const QString& ticket, const QJsonObject& event) { received.append(qMakePair(ticket, event)); });
        CTEST_CHECK(connection);
        gateway.setReady("model://iisaccson-fixture", [&](const QJsonArray &messages, const QJsonObject &, ResidentGateway::Callback callback) {
            const auto input = messages.first().toObject().value("content").toString();
            callbacks.insert(input, std::move(callback));
            return [&, input] { cancelled.append(input); };
        });
        QString error;
        const auto a = gateway.start({QJsonObject{{"role", "user"}, {"content", "app A"}}}, {}, &error);
        const auto b = gateway.start({QJsonObject{{"role", "user"}, {"content", "app B"}}}, {}, &error);
        CTEST_CHECK_MESSAGE(!a.isEmpty() && !b.isEmpty() && a != b, qPrintable(error));
        callbacks["app B"]({{"kind", "delta"}, {"text", "B only"}});
        callbacks["app A"]({{"kind", "finished"}, {"text", "A only"}});
        CTEST_WAIT_COMPARE(received.size(), 2);
        CTEST_COMPARE(received.at(0).first, b);
        CTEST_COMPARE(received.at(0).second.value("text").toString(), "B only");
        CTEST_COMPARE(received.at(1).first, a);
        gateway.setLoading("model://next");
        CTEST_COMPARE(cancelled, QStringList{"app B"});
        CTEST_WAIT_COMPARE(received.size(), 3);
        CTEST_COMPARE(received.at(2).first, b);
        CTEST_COMPARE(received.at(2).second.value("kind").toString(), "finished");
        callbacks["app B"]({{"kind", "delta"}, {"text", "stale"}});
        callbacks["app B"]({{"kind", "finished"}, {"text", "stale"}});
        QCoreApplication::processEvents();
        CTEST_COMPARE(received.size(), 3);
        CTEST_CHECK(gateway.start({QJsonObject{{"role", "user"}, {"content", "x"}}}, {}, &error).isEmpty());
        CTEST_CHECK(!error.isEmpty());
    }
    void slowConsumersHaveABoundedQueue() {
        int cancelled=0;
        QList<QPair<QString, QJsonObject>> received;
        ResidentGateway gateway;
        const auto connection = QObject::connect(&gateway, &ResidentGateway::event, &gateway,
            [&](const QString& ticket, const QJsonObject& event) { received.append(qMakePair(ticket, event)); });
        CTEST_CHECK(connection);
        gateway.setReady("model://fixture",[&](const QJsonArray &,const QJsonObject &,ResidentGateway::Callback callback) {
            for (int i=0;i<1100;++i) callback({{"kind","delta"},{"text","x"}});
            return [&] { ++cancelled; };
        });
        const auto ticket=gateway.start({QJsonObject{{"role","user"},{"content","test"}}},{});
        CTEST_CHECK(!ticket.isEmpty());
        CTEST_WAIT_DEFAULT(!received.isEmpty() && received.last().second.value("kind")=="finished");
        CTEST_CHECK(received.size()<=1025); CTEST_COMPARE(cancelled,1);
        CTEST_CHECK(!received.last().second.value("error").toString().isEmpty());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    society_ctest::Runner runner(argc == 2 ? argv[1] : "");
    ResidentGatewayTest test;
    runner.run("independentRequestsAndDriveChange", [&] { const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }); test.independentRequestsAndDriveChange(); });
    runner.run("slowConsumersHaveABoundedQueue", [&] { const auto cleanup = qScopeGuard([] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }); test.slowConsumersHaveABoundedQueue(); });
    return runner.result();
}
