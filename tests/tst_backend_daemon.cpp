#include <DeliveryStore.h>
#include <SocietyDrive.h>
#include <iiSocietyHelper.h>
#include "Society/Files/ModelImporter.h"
#include "Shared/LocalBackendAddress.h"
#include "Shared/LocalBackendPeer.h"
#include "Shared/BackendCommand.h"
#include "Daemon/ModelBackendServer.h"
#include "Daemon/NativeDirectorySession.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaProperty>
#include <QProcess>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <QUuid>
#include <QtEndian>
#include <chrono>
#include <future>

class BackendDaemonTests final : public QObject {
    Q_OBJECT
    static QJsonObject request(QLocalSocket &socket, const QJsonObject &body)
    {
        const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        QByteArray frame(4, '\0');
        qToBigEndian(quint32(payload.size()), frame.data());
        if (socket.write(frame + payload) != frame.size() + payload.size()) return {};
        socket.flush();
        QByteArray input;
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 10000 && socket.state() == QLocalSocket::ConnectedState) {
            QTest::qWait(10);
            input += socket.readAll();
            while (input.size() >= 4) {
                const auto size = qFromBigEndian<quint32>(input.constData());
                if (!size || size > 1024 * 1024) return {};
                if (input.size() < int(size + 4)) break;
                const auto message = QJsonDocument::fromJson(input.mid(4, size)).object();
                input.remove(0, size + 4);
                if (message.value("id") == body.value("id")) return message;
            }
        }
        return {};
    }
private slots:
    void localPeerIdentityIsObtainedFromTheSocket()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.peer-identity-XXXXXX"));
        QVERIFY(fixture.isValid());
        QLocalServer server; QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        QLocalSocket client; client.connectToServer(societyBackendAddress(fixture.path()));
        QVERIFY(client.waitForConnected(5000));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(),5000);
        std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection()); QVERIFY(peer);
#if defined(Q_OS_DARWIN) || defined(Q_OS_LINUX)
        QVERIFY(societyBackendPeerSameUser(peer.get()));
        QCOMPARE(societyBackendPeerPid(peer.get()),QCoreApplication::applicationPid());
        QVERIFY(societyBackendPeerMatchesExecutable(peer.get(),QCoreApplication::applicationFilePath()));
        QVERIFY(!societyBackendPeerMatchesExecutable(peer.get(),fixture.filePath("not-the-peer")));
#else
        QVERIFY(societyBackendPeerPid(peer.get()) < 0);
#endif
    }
    void headlessMutationTimeoutRemainsUncertain()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.file-timeout-XXXXXX"));
        QVERIFY(fixture.isValid());
        QLocalServer server; QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        QPointer<QLocalSocket> peer;
        connect(&server,&QLocalServer::newConnection,this,[&] {
            peer=server.nextPendingConnection();
            const auto body=QJsonDocument(QJsonObject{{"type","state"},{"state",QJsonObject{{"selectedContainer","/fixture/drive"}}}}).toJson(QJsonDocument::Compact);
            QByteArray header(4,'\0'); qToBigEndian(quint32(body.size()),header.data());
            peer->write(header.left(2)); peer->flush();
            QTimer::singleShot(10,peer,[peer,header,body] { if(peer) { peer->write(header.mid(2)+body); peer->flush(); } });
        });
        auto future=std::async(std::launch::async,[&] {
            return societyBackendCommand(fixture.path(),"fileOperation",{{"action","trash"},{"path","Files/a"}},"/fixture/drive",700);
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,5000);
        const auto result=future.get(); QVERIFY(!result.ok()); QVERIFY(result.uncertain);
    }
    void headlessImportTimeoutRemainsUncertain()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.model-timeout-XXXXXX"));
        QVERIFY(fixture.isValid());
        QLocalServer server; QVERIFY(server.listen(societyBackendAddress(fixture.path())));
        QPointer<QLocalSocket> peer;
        connect(&server,&QLocalServer::newConnection,this,[&] {
            peer=server.nextPendingConnection();
            const auto body=QJsonDocument(QJsonObject{{"type","state"},{"state",QJsonObject{{"selectedContainer","/fixture/drive"}}}}).toJson(QJsonDocument::Compact);
            QByteArray header(4,'\0'); qToBigEndian(quint32(body.size()),header.data());
            peer->write(header+body); peer->flush();
        });
        auto future=std::async(std::launch::async,[&] {
            return societyBackendCommand(fixture.path(),"import",{{"sources",QJsonArray{"/source/model.safetensors"}}},"/fixture/drive",700);
        });
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,5000);
        const auto result=future.get(); QVERIFY(!result.ok()); QVERIFY(result.uncertain);
    }
    void headlessFileCommandsUseTheSelectedContainer()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.file-commands-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto previous=qgetenv("SOCIETY_JOB_STATE_ROOT"); const bool hadPrevious=qEnvironmentVariableIsSet("SOCIETY_JOB_STATE_ROOT");
        qputenv("SOCIETY_JOB_STATE_ROOT",fixture.filePath("jobs").toUtf8());
        auto restore=qScopeGuard([&]{if(hadPrevious)qputenv("SOCIETY_JOB_STATE_ROOT",previous);else qunsetenv("SOCIETY_JOB_STATE_ROOT");});
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/original.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        const auto broker=fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QVERIFY(QFile::setPermissions(broker,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        ModelBackendServer server; server.selectContainer(root); QVERIFY(server.listen(broker));
        auto invoke=[&](QString op,QJsonObject args={},QString expected=QString()) {
            return std::async(std::launch::async,[=] { return societyBackendCommand(broker,op,args,expected,15000); });
        };
        auto mismatch=invoke("fileOperation",{{"action","trash"},{"path","Files/original.txt"}},fixture.filePath("other"));
        QTRY_VERIFY_WITH_TIMEOUT(mismatch.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,20000);
        const auto denied=mismatch.get(); QVERIFY(!denied.ok()); QVERIFY(!denied.uncertain); QVERIFY(QFileInfo::exists(file.fileName()));
        auto pending=invoke("fileOperation",{{"action","rename"},{"path","Files/original.txt"},{"argument","renamed.txt"}},root);
        QTRY_VERIFY_WITH_TIMEOUT(pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,20000);
        const auto accepted=pending.get(); QVERIFY2(accepted.ok(),qPrintable(accepted.error));
        QCOMPARE(accepted.response.value("state").toString(),QStringLiteral("queued"));
        const auto id=accepted.response.value("jobId").toString(); QVERIFY(!id.isEmpty());
        bool complete=false; SocietyBackendCommandReply last;
        QElapsedTimer completionDeadline; completionDeadline.start();
        while (completionDeadline.elapsed()<30000 && !complete) {
            auto query=invoke("fileJobStatus",{{"jobId",id}},root);
            QTRY_VERIFY_WITH_TIMEOUT(query.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,20000);
            last=query.get();
            const auto jobState=last.response.value("job").toObject().value("state").toString();
            complete=last.ok() && jobState=="succeeded";
            if (jobState=="failed" || jobState=="partial" || jobState=="attention" || jobState=="cancelled") break;
            if (!complete) QTest::qWait(100);
        }
        QVERIFY2(complete,qPrintable(last.error+" "+QString::fromUtf8(QJsonDocument(last.response).toJson(QJsonDocument::Compact))));
        QVERIFY(QFileInfo::exists(root+"/Files/renamed.txt"));
        auto invalid=invoke("fileOperation",{{"action","trash"},{"path","Files/../escape"}},root);
        QTRY_VERIFY_WITH_TIMEOUT(invalid.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,20000);
        QVERIFY(!invalid.get().ok());
#ifdef SOCIETY_CLI_EXECUTABLE
        QProcess command;
        command.start(QStringLiteral(SOCIETY_CLI_EXECUTABLE),{"--broker",broker,"status"});
        QVERIFY(command.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(command.state(),QProcess::NotRunning,20000);
        QCOMPARE(command.exitCode(),0);
        QVERIFY(QJsonDocument::fromJson(command.readAllStandardOutput()).object().value("accepted").toBool());
        QProcess badContainer;
        badContainer.start(QStringLiteral(SOCIETY_CLI_EXECUTABLE),{"--broker",broker,"file-job","submit","trash","Files/missing.txt","--container",""});
        QVERIFY(badContainer.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(badContainer.state(),QProcess::NotRunning,20000);
        QCOMPARE(badContainer.exitCode(),1); QVERIFY(QFileInfo::exists(root+"/Files/renamed.txt"));
        QFile outside(fixture.filePath("outside.txt")); QVERIFY(outside.open(QIODevice::WriteOnly)); outside.write("fixture"); outside.close();
        const auto source=QFileInfo(outside).canonicalFilePath(); QProcess copy;
        copy.start(QStringLiteral(SOCIETY_CLI_EXECUTABLE),{"--broker",broker,"file-job","submit","copy",source,"--argument","Files","--container",root});
        QVERIFY(copy.waitForStarted()); QTRY_COMPARE_WITH_TIMEOUT(copy.state(),QProcess::NotRunning,20000);
        QCOMPARE(copy.exitCode(),0); QVERIFY(QJsonDocument::fromJson(copy.readAllStandardOutput()).object().value("accepted").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root+"/Files/outside.txt"),60000); QVERIFY(QFileInfo::exists(source));
        QFile pythonFile(root+"/Files/python.txt"); QVERIFY(pythonFile.open(QIODevice::WriteOnly)); pythonFile.write("fixture"); pythonFile.close();
        QProcess python;
        python.start(QStringLiteral(SOCIETY_PYTHON_EXECUTABLE),{"-B",QStringLiteral(SOCIETY_SOURCE_DIRECTORY "/tests/python_client_probe.py"),
            QStringLiteral(SOCIETY_SOURCE_DIRECTORY "/App/Society/Bindings/society.py"),QStringLiteral(SOCIETY_CLI_EXECUTABLE),broker,root});
        QVERIFY(python.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(python.state(),QProcess::NotRunning,45000);
        QVERIFY2(python.exitCode()==0,python.readAllStandardError().constData());
        QVERIFY(QFileInfo::exists(root+"/Files/python-renamed.txt"));
#endif
    }
    void nativeFileJobsSurviveClientDisconnect()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.file-jobs-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto previous=qgetenv("SOCIETY_JOB_STATE_ROOT"); const bool hadPrevious=qEnvironmentVariableIsSet("SOCIETY_JOB_STATE_ROOT");
        qputenv("SOCIETY_JOB_STATE_ROOT",fixture.filePath("jobs").toUtf8());
        auto restore=qScopeGuard([&]{if(hadPrevious)qputenv("SOCIETY_JOB_STATE_ROOT",previous);else qunsetenv("SOCIETY_JOB_STATE_ROOT");});
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/original.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        const auto broker=fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QVERIFY(QFile::setPermissions(broker,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        ModelBackendServer server; server.selectContainer(root); QVERIFY(server.listen(broker));
        QLocalSocket client; client.connectToServer(societyBackendAddress(broker)); QVERIFY(client.waitForConnected(5000));
        const QJsonObject operation{{"id","unique-file-operation"},{"op","fileOperation"},{"container",root},
                                    {"action","rename"},{"path","Files/original.txt"},{"argument","renamed.txt"}};
        const auto first=request(client,operation); QVERIFY2(first.value("accepted").toBool(),qPrintable(first.value("error").toString()));
        const auto jobId=first.value("jobId").toString(); QVERIFY(!jobId.isEmpty());
        QCOMPARE(request(client,operation).value("jobId").toString(),jobId);
        client.disconnectFromServer(); QLocalSocket observer; observer.connectToServer(societyBackendAddress(broker)); QVERIFY(observer.waitForConnected(5000));
        QJsonObject status;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{
            status=request(observer,{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                     {"op","fileJobStatus"},{"container",root},{"jobId",jobId}});
            return status.value("job").toObject().value("state").toString()=="succeeded";
        })(),30000);
        QCOMPARE(status.value("job").toObject().value("path").toString(),QStringLiteral("Files/renamed.txt"));
        QVERIFY(QFileInfo::exists(root+"/Files/renamed.txt")); QVERIFY(!QFileInfo::exists(file.fileName()));
        auto invalid=operation; invalid.insert("id","invalid-file-operation"); invalid.insert("path","Files/../outside");
        QVERIFY(!request(observer,invalid).value("accepted").toBool());
    }
    void nativeDirectoryReplyCacheIsBounded()
    {
        NativeDirectorySession session;
        const QJsonObject reply{{"entries", QString(256 * 1024, QLatin1Char('x'))}};
        for (int i = 0; i < 40; ++i)
            session.rememberReply(QString::number(i), QByteArray("request"), reply);
        QVERIFY(!session.cachedReply("0"));
        QVERIFY(session.cachedReply("39"));
    }
    void nativeDirectoryQueriesAreRevisionBound()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.directory-query-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QVERIFY(QDir().mkdir(root+"/Files/Alpha folder"));
        for (const auto &name : {"alpha.png","Beta.json","Draft*.txt"}) {
            QFile file(root+"/Files/"+name); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        NativeDirectorySession session; QString error;
        QVERIFY2(session.open(root,{{"path","Files"},{"query","ALPHA"},{"sort","name"},{"descending",false}},&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(session.state().value("status").toString(),QStringLiteral("ready"),30000);
        auto state=session.state(); QCOMPARE(state.value("count").toInt(),2);
        const auto generation=state.value("generation").toString(), revision=state.value("revision").toString();
        auto page=session.page({{"generation",generation},{"revision",revision},{"offset",0},{"limit",64}},&error);
        QCOMPARE(page.value("entries").toArray().size(),2);
        QVERIFY2(session.open(root,{{"path","Files"},{"query","*"},{"sort","name"},{"descending",false}},&error),qPrintable(error));
        QVERIFY(session.page({{"generation",generation},{"revision",revision},{"offset",0},{"limit",64}},&error).isEmpty());
        error.clear(); QTRY_COMPARE_WITH_TIMEOUT(session.state().value("status").toString(),QStringLiteral("ready"),30000);
        state=session.state(); QCOMPARE(state.value("count").toInt(),1);
        page=session.page({{"generation",state.value("generation")},{"revision",state.value("revision")},{"offset",0},{"limit",64}},&error);
        QCOMPARE(page.value("entries").toArray().first().toObject().value("name").toString(),QStringLiteral("Draft*.txt"));
        QVERIFY2(session.open(root,{{"path","Files"},{"query",""},{"sort","kind"},{"descending",false}},&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(session.state().value("status").toString(),QStringLiteral("ready"),30000);
        state=session.state(); page=session.page({{"generation",state.value("generation")},{"revision",state.value("revision")},{"offset",0},{"limit",64}},&error);
        QCOMPARE(page.value("entries").toArray().size(),4);
        QCOMPARE(page.value("entries").toArray().at(1).toObject().value("name").toString(),QStringLiteral("Beta.json"));
        QVERIFY(!session.open(root,{{"path","Files"},{"sort","invalid"}},&error));
    }
    void nativeDirectoryActivatesOnlyCurrentFiles()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.directory-activate-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        QFile file(root+"/Files/example.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        NativeDirectorySession session; QString error;
        QVERIFY2(session.open(root,{{"path","Files"}},&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(session.state().value("status").toString(),QStringLiteral("ready"),30000);
        const auto state=session.state(); QSignalSpy opened(&session,&NativeDirectorySession::openResult);
        const QJsonObject request{{"id","open-1"},{"generation",state.value("generation")},{"revision",state.value("revision")},{"path","Files/example.txt"}};
        QVERIFY2(session.activate(request,&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(),1,10000);
        const auto result=opened.first().first().toJsonObject();
        QCOMPARE(result.value("id").toString(),QStringLiteral("open-1"));
        QCOMPARE(result.value("path").toString(),file.fileName()); QVERIFY(result.value("error").toString().isEmpty());
        auto invalid=request; invalid.insert("path","Files/../outside"); QVERIFY(!session.activate(invalid,&error));
        invalid=request; invalid.insert("revision","old"); QVERIFY(!session.activate(invalid,&error));
    }
    void nativeDirectorySessionsExposeBoundedIndependentSnapshots()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto container = fixture.filePath("Test.societycontainer");
        QVERIFY(QDir().mkdir(container));
        const auto drive = iiSocietyContainer::SocietyDrive::create(container); QVERIFY(drive);
        const auto files = drive->sectionPath(iiSocietyContainer::StoreSection::Files);
        QVERIFY(QDir().mkpath(QDir(files).filePath("Folder")));
        for (const auto &name : {"alpha.txt", "beta.txt", "alpha.txt.iisacc.json"}) {
            QFile file(QDir(files).filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        const auto outside = fixture.filePath("outside"); QVERIFY(QDir().mkdir(outside));
        QVERIFY(QFile::link(outside, QDir(files).filePath("redirect")));
        const auto broker = fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QVERIFY(QFile::setPermissions(broker, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        ModelBackendServer server; server.selectContainer(container); QVERIFY(server.listen(broker));
        QLocalSocket first, second;
        first.connectToServer(societyBackendAddress(broker)); second.connectToServer(societyBackendAddress(broker));
        QVERIFY(first.waitForConnected(5000)); QVERIFY(second.waitForConnected(5000));
        auto call = [&](QLocalSocket &socket, const QString &op, QJsonObject args = {}) {
            args.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
            args.insert("op", op); args.insert("container", container); return request(socket, args);
        };
        const auto opened = call(first, "directoryOpen", {{"path", "Files"}});
        QVERIFY2(opened.value("accepted").toBool(), qPrintable(QString::fromUtf8(QJsonDocument(opened).toJson())));
        QVERIFY(!opened.contains("state")); // File browsing does not serialize the unrelated model job list.
        const auto generation = opened.value("directory").toObject().value("generation").toString();
        QVERIFY(!generation.isEmpty());
        const auto other = call(second, "directoryOpen", {{"path", "Files/Folder"}});
        QVERIFY(other.value("accepted").toBool());
        const auto otherGeneration = other.value("directory").toObject().value("generation").toString();
        QJsonObject state;
        QTRY_VERIFY_WITH_TIMEOUT(([&] { state = call(first,"directoryState").value("directory").toObject();
            return state.value("status").toString() == "ready" && state.value("count").toInt() == 3; })(), 15000);
        QCOMPARE(state.value("path").toString(), QString("Files"));
        const auto rev = state.value("revision").toString(); QVERIFY(!rev.isEmpty());
        const auto page = call(first,"directoryPage", {{"generation", generation}, {"revision", rev}, {"offset", 0}, {"limit", 2}});
        QVERIFY(page.value("accepted").toBool()); QCOMPARE(page.value("entries").toArray().size(), 2);
        const auto later = call(first,"directoryPage", {{"generation", generation}, {"revision", rev}, {"offset", 2}, {"limit", 2}});
        QVERIFY(later.value("accepted").toBool()); QCOMPARE(later.value("entries").toArray().size(), 1);
        QSet<QString> names;
        for (const auto &v : page.value("entries").toArray()) names.insert(v.toObject().value("name").toString());
        for (const auto &v : later.value("entries").toArray()) names.insert(v.toObject().value("name").toString());
        QCOMPARE(names, QSet<QString>({"Folder", "alpha.txt", "beta.txt"}));
        QVERIFY(!call(first,"directoryPage", {{"generation", generation},{"revision", rev},{"offset", 0},{"limit", 65}}).value("accepted").toBool());
        QVERIFY(!call(first,"directoryOpen", {{"path", "Files/../Models"}}).value("accepted").toBool());
        QVERIFY(!call(first,"directoryOpen", {{"path", "Files/redirect"}}).value("accepted").toBool());
        QVERIFY(!call(first,"directoryOpen", {{"path", ".society-runtime"}}).value("accepted").toBool());
        const auto state2 = call(second,"directoryState").value("directory").toObject();
        QCOMPARE(state2.value("path").toString(), QString("Files/Folder"));
        QCOMPARE(state2.value("generation").toString(), otherGeneration);
        // Identical client-local IDs must never replay another window's view.
        const QJsonObject sameId{{"id", "same-client-id"}, {"op", "directoryState"}, {"container", container}};
        QCOMPARE(request(first, sameId).value("directory").toObject().value("path").toString(), QString("Files"));
        QCOMPARE(request(second, sameId).value("directory").toObject().value("path").toString(), QString("Files/Folder"));
        QFile added(QDir(files).filePath("gamma.txt")); QVERIFY(added.open(QIODevice::WriteOnly));
        added.write("new"); added.close();
        QTRY_VERIFY_WITH_TIMEOUT(([&] { state = call(first,"directoryState").value("directory").toObject();
            return state.value("status").toString() == "ready" && state.value("count").toInt() == 4
                && state.value("revision").toString() != rev; })(), 15000);
        QVERIFY(!call(first,"directoryPage", {{"generation", generation},{"revision", rev},{"offset", 0},{"limit", 2}}).value("accepted").toBool());
        const auto reopened = call(first,"directoryOpen", {{"path", "Models"}});
        QVERIFY(reopened.value("accepted").toBool());
        QVERIFY(!call(first,"directoryPage", {{"generation", generation},{"revision", rev},{"offset", 0},{"limit", 2}}).value("accepted").toBool());
        QCOMPARE(call(second,"directoryState").value("directory").toObject().value("path").toString(), QString("Files/Folder"));
        server.selectContainer({});
        QVERIFY(!call(second,"directoryState").value("accepted").toBool());
    }

    void reconnectRequestsAreValidatedAndDispatchedToTheWorker()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto container = fixture.filePath("Test.societycontainer");
        QVERIFY(QDir().mkdir(container));
        const auto drive = iiSocietyContainer::SocietyDrive::create(container); QVERIFY(drive);
        const auto broker = fixture.filePath("broker"); QVERIFY(QDir().mkdir(broker));
        QVERIFY(QFile::setPermissions(broker, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        ModelBackendServer server;
        server.selectContainer(container);
        server.setSystemDriveState({{"sourcePath", container}, {"identifier", drive->identifier()}, {"ready", true}});
        QSignalSpy requested(&server, &ModelBackendServer::systemDriveConnectRequested);
        QVERIFY(server.listen(broker));
        QLocalSocket socket; socket.connectToServer(societyBackendAddress(broker));
        QVERIFY(socket.waitForConnected(5000));
        const auto wrong = request(socket, {{"id", "wrong"}, {"op", "systemDriveReconnect"},
                                           {"container", container}, {"identifier", "b43e17fc-a2a1-4b61-8935-79715770e150"}});
        QVERIFY(!wrong.value("accepted").toBool());
        QCOMPARE(requested.size(), 0);
        const QJsonObject valid{{"id", "retry"}, {"op", "systemDriveReconnect"},
                                {"container", container}, {"identifier", drive->identifier()}};
        QVERIFY(request(socket, valid).value("accepted").toBool());
        QCOMPARE(requested.size(), 1);
        QVERIFY(request(socket, valid).value("accepted").toBool());
        QCOMPARE(requested.size(), 1); // The same request id is not executed twice.
    }

    void onDemandBrokerAlsoRunsTheBackendWithoutAWindow()
    {
        // Unix-domain socket paths are limited on macOS. Keep the isolated
        // fixture beneath resources while leaving room for the socket name.
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto container = fixture.filePath("Test.societycontainer");
        QVERIFY(QDir().mkdir(container));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(container));
        const auto helperPath = QDir(container).filePath(".society-runtime/helpers/v1");
        const auto brokerPath = fixture.filePath("broker");
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SOCIETY_CONTAINER_PATH", container);
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("storage.json"));
        environment.insert("SOCIETY_LOCAL_BROKER_DIR", brokerPath);
        environment.insert("SOCIETY_TEST_OUTPUT_DIR", fixture.path());
        environment.insert("SOCIETY_JOB_STATE_ROOT", fixture.filePath("jobs"));
        environment.insert("SOCIETY_DISABLE_SESSION_RESTORE", "1");
        QProcess daemon;
        daemon.setProcessEnvironment(environment);
        daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--license-broker"});
        const auto cleanup = qScopeGuard([&] {
            if (daemon.state() != QProcess::NotRunning) {
                daemon.terminate();
                if (!daemon.waitForFinished(4000)) { daemon.kill(); daemon.waitForFinished(4000); }
            }
        });
        QVERIFY(daemon.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(brokerPath + "/broker.sock"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(brokerPath + "/backend.sock"), 10000);
        QLocalSocket backend;
        backend.connectToServer(brokerPath + "/backend.sock");
        QVERIFY(backend.waitForConnected(5000));
        const auto snapshot = request(backend, {{"id", "drive-state"}, {"op", "snapshot"}, {"container", container}});
        QVERIFY(snapshot.value("accepted").toBool());
        // Product tests explicitly disable the OS adapter; the backend still
        // publishes its state without mounting the disposable fixture.
        QVERIFY(snapshot.value("state").toObject().value("systemDrive").isObject());
        QVERIFY(snapshot.value("state").toObject().value("systemDrive").toObject().isEmpty());
        const auto selected = iiSocietyContainer::SocietyDrive::open(container); QVERIFY(selected);
        const auto unavailable = request(backend, {{"id", "disabled-drive"}, {"op", "systemDriveReconnect"},
                                                   {"container", container}, {"identifier", selected->identifier()}});
        QVERIFY(!unavailable.value("accepted").toBool());
        // The SDK's delivery transport is restricted to the selected container.
        qputenv("SOCIETY_CONTAINER_PATH", container.toUtf8());
        iiSocietyHelper::Helper sender;
        QVERIFY(sender.start({"com.iisacc.backend-test", "Backend test", "1"}, {helperPath, 50, 5000}));
        const auto message = sender.sendData("backend.test", {{"value", "without-window"}});
        QVERIFY(!message.isEmpty());
        iiSocietyHelper::DeliveryStore store;
        QVERIFY(store.open(helperPath));
        const auto received = [&] {
            for (const auto &item : store.readAfter(0))
                if (item.toMap().value("id").toString() == message) return true;
            return false;
        };
        QElapsedTimer deadline;
        deadline.start();
        while (!received() && daemon.state() == QProcess::Running && deadline.elapsed() < 10000)
            QTest::qWait(50);
        QVERIFY2(received(), daemon.readAllStandardError().constData());
        QLockFile networkOwner(QDir(container).filePath(".society-runtime/sync/owner.lock"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(networkOwner.fileName()), 10000);
        QVERIFY(!networkOwner.tryLock());
        QCOMPARE(daemon.state(), QProcess::Running);
    }
    void modelImportContinuesAfterTheClientDisconnects()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto container = fixture.filePath("Test.societycontainer");
        QVERIFY(QDir().mkdir(container));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(container));
        const auto source = fixture.filePath("model.safetensors");
        const QByteArray payload(2 * 1024 * 1024, 'm');
        const auto header = QJsonDocument(QJsonObject{{"tensor", QJsonObject{{"dtype", "U8"},
            {"shape", QJsonArray{payload.size()}}, {"data_offsets", QJsonArray{0, payload.size()}}}}}).toJson(QJsonDocument::Compact);
        QByteArray prefix(8, '\0'); qToLittleEndian(quint64(header.size()), prefix.data());
        QFile input(source); QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(prefix + header + payload), qint64(prefix.size() + header.size() + payload.size()));
        input.close();
        QFile metadata(source + ".iisacc.json"); QVERIFY(metadata.open(QIODevice::WriteOnly));
        QVERIFY(metadata.write("{\"format\":\"iiFileProvider.Metadata\",\"schema\":1,"
            "\"fileKey\":\"file-0123456789abcdef0123456789abcdef\",\"revision\":1,"
            "\"metadata\":{\"title\":\"fixture\"},\"collections\":[]}") > 0); metadata.close();
        const auto broker = fixture.filePath("broker");
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SOCIETY_CONTAINER_PATH", container);
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("storage.json"));
        environment.insert("SOCIETY_LOCAL_BROKER_DIR", broker);
        environment.insert("SOCIETY_TEST_OUTPUT_DIR", fixture.path());
        environment.insert("SOCIETY_JOB_STATE_ROOT", fixture.filePath("jobs"));
        environment.insert("SOCIETY_DISABLE_SESSION_RESTORE", "1");
        QProcess daemon; daemon.setProcessEnvironment(environment);
        daemon.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--license-broker"});
        const auto cleanup = qScopeGuard([&] {
            if (daemon.state() != QProcess::NotRunning) {
                daemon.terminate();
                if (!daemon.waitForFinished(4000)) { daemon.kill(); daemon.waitForFinished(4000); }
            }
        });
        QVERIFY(daemon.waitForStarted(5000));
        QElapsedTimer deadline; deadline.start();
        while (!QFileInfo::exists(broker + "/backend.sock") && daemon.state() == QProcess::Running && deadline.elapsed() < 10000)
            QTest::qWait(50);
        QVERIFY2(QFileInfo::exists(broker + "/backend.sock"), daemon.readAllStandardError().constData());
        QLocalSocket client; client.connectToServer(broker + "/backend.sock");
        QVERIFY(client.waitForConnected(5000));
        const auto rejected = request(client, {{"id", "reject"}, {"op", "import"},
            {"container", fixture.path()}, {"sources", QJsonArray{source}}});
        QVERIFY(!rejected.value("accepted").toBool());
        client.abort();
        const auto oldBroker = qgetenv("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldAutoStart = qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const bool hadBroker = qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        const bool hadAutoStart = qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restoreEnvironment = qScopeGuard([&] {
            if (hadBroker) qputenv("SOCIETY_LOCAL_BROKER_DIR", oldBroker); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR");
            if (hadAutoStart) qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", oldAutoStart); else qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR", broker.toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", "1");
        QString job;
        {
            qmlRegisterType<ModelImporter>("Society.BackendFixture", 1, 0, "ModelImporter");
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData("import QtQml\nimport Society.BackendFixture 1.0\n"
                "ModelImporter { property url testSource; function startTest() { return importFiles([testSource]) } }", {});
            std::unique_ptr<QObject> object(component.createWithInitialProperties({{"backendManaged", true}, {"containerPath", container}}));
            QVERIFY2(object, qPrintable(component.errorString()));
            auto *importer = qobject_cast<ModelImporter *>(object.get()); QVERIFY(importer);
            QVERIFY(object->setProperty("testSource", QUrl::fromLocalFile(source)));
            QTRY_VERIFY_WITH_TIMEOUT(importer->backendConnected(), 10000);
            QVariant started;
            QVERIFY(QMetaObject::invokeMethod(object.get(), "startTest", Q_RETURN_ARG(QVariant, started)));
            QVERIFY(started.toBool());
            const auto busyProperty = importer->metaObject()->property(importer->metaObject()->indexOfProperty("busy"));
            QVERIFY(busyProperty.isValid());
            QCOMPARE(busyProperty.read(importer).toBool(), importer->busy());
            QTRY_VERIFY_WITH_TIMEOUT(!importer->currentJobId().isEmpty(), 10000);
            job = importer->currentJobId();
        } // Closing the GUI adapter must not cancel the backend's job.
        const auto imported = [&] {
            QDirIterator it(QDir(container).filePath("Models"), {"model.safetensors"}, QDir::Files, QDirIterator::Subdirectories);
            return it.hasNext() ? it.next() : QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!imported().isEmpty(), 20000);
        QVERIFY(QFileInfo::exists(source));
        const auto classification = [&] { return container + "/.society-runtime/classifications/models/"
            + QDir(container + "/Models").relativeFilePath(imported()) + ".iisacc.json"; };
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(classification()), 20000);
        QVERIFY(!QFileInfo::exists(imported() + ".iisacc.json"));
        QFile copiedMetadata(classification());
        QVERIFY(copiedMetadata.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(copiedMetadata.readAll()).object().value("metadata").toObject()
            .value("title").toString(), QStringLiteral("fixture"));
        ModelImporter reopened;
        reopened.setBackendManaged(true);
        reopened.setContainerPath(container);
        QTRY_VERIFY_WITH_TIMEOUT(reopened.backendConnected(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(reopened.currentJobId(), job, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.busy(), 10000);
        QVERIFY2(reopened.errorString().isEmpty(), qPrintable(reopened.errorString()));
        QCOMPARE(daemon.state(), QProcess::Running);
    }
    void anUnresponsiveBackendDoesNotLeaveTheClientPermanentlyBusy()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        QLocalServer server;
        QVERIFY(server.listen(fixture.filePath("backend.sock")));
        const auto oldBroker = qgetenv("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldAutoStart = qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const bool hadBroker = qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        const bool hadAutoStart = qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restoreEnvironment = qScopeGuard([&] {
            if (hadBroker) qputenv("SOCIETY_LOCAL_BROKER_DIR", oldBroker); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR");
            if (hadAutoStart) qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", oldAutoStart); else qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR", fixture.path().toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", "1");
        ModelBackendClient client(nullptr, 100);
        client.setContainerPath(fixture.filePath("test.societycontainer"));
        client.start();
        QTRY_VERIFY(client.connected());
        QSignalSpy error(&client, &ModelBackendClient::requestError);
        QVERIFY(client.request("import", {}, true));
        QVERIFY(client.pendingMutation());
        QTRY_VERIFY_WITH_TIMEOUT(!client.pendingMutation(), 2000);
        QVERIFY(!error.isEmpty());
        const auto message = error.first().at(0).toString();
        QVERIFY(message.contains("did not respond", Qt::CaseInsensitive));
        QVERIFY(message.contains("operation status", Qt::CaseInsensitive));
    }
    void disconnectedBackendDoesNotLeaveAnOldBusyStateInTheView()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        QLocalServer server; QVERIFY(server.listen(fixture.filePath("backend.sock")));
        const auto oldBroker = qgetenv("SOCIETY_LOCAL_BROKER_DIR");
        const auto oldAutoStart = qgetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const bool hadBroker = qEnvironmentVariableIsSet("SOCIETY_LOCAL_BROKER_DIR");
        const bool hadAutoStart = qEnvironmentVariableIsSet("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        const auto restoreEnvironment = qScopeGuard([&] {
            if (hadBroker) qputenv("SOCIETY_LOCAL_BROKER_DIR", oldBroker); else qunsetenv("SOCIETY_LOCAL_BROKER_DIR");
            if (hadAutoStart) qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", oldAutoStart); else qunsetenv("SOCIETY_DISABLE_BACKEND_AUTOSTART");
        });
        qputenv("SOCIETY_LOCAL_BROKER_DIR", fixture.path().toUtf8());
        qputenv("SOCIETY_DISABLE_BACKEND_AUTOSTART", "1");
        const auto container = fixture.filePath("test.societycontainer");
        ModelImporter view; view.setBackendManaged(true); view.setContainerPath(container);
        QTRY_VERIFY(view.backendConnected());
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        const auto body = QJsonDocument(QJsonObject{{"type", "state"}, {"state", QJsonObject{
            {"containerPath", container}, {"selectedContainer", container}, {"busy", true}}}}).toJson(QJsonDocument::Compact);
        QByteArray frame(4, '\0'); qToBigEndian(quint32(body.size()), frame.data());
        QCOMPARE(peer->write(frame + body), qint64(frame.size() + body.size())); peer->flush();
        QTRY_VERIFY(view.busy());
        peer->abort();
        QTRY_VERIFY(!view.backendConnected());
        QVERIFY(!view.busy());
        QVERIFY(!view.errorString().isEmpty());
    }
    void loginServiceTakesOverWhenTheOnDemandBackendStops()
    {
        QTemporaryDir fixture(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY) + "/../../.backend-XXXXXX"));
        QVERIFY(fixture.isValid());
        const auto container = fixture.filePath("Test.societycontainer");
        QVERIFY(QDir().mkdir(container));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(container));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SOCIETY_CONTAINER_PATH", container);
        environment.insert("SOCIETY_STORAGE_SETTINGS_PATH", fixture.filePath("storage.json"));
        environment.insert("SOCIETY_LOCAL_BROKER_DIR", fixture.filePath("broker"));
        environment.insert("SOCIETY_TEST_OUTPUT_DIR", fixture.path());
        environment.insert("SOCIETY_JOB_STATE_ROOT", fixture.filePath("jobs"));
        environment.insert("SOCIETY_DISABLE_SESSION_RESTORE", "1");
        QProcess onDemand, login;
        onDemand.setProcessEnvironment(environment); login.setProcessEnvironment(environment);
        const auto stop = [](QProcess &process) {
            if (process.state() == QProcess::NotRunning) return;
            process.terminate();
            if (!process.waitForFinished(4000)) { process.kill(); process.waitForFinished(4000); }
        };
        const auto cleanup = qScopeGuard([&] { stop(login); stop(onDemand); });
        onDemand.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--license-broker"});
        QVERIFY(onDemand.waitForStarted(5000));
        const auto onDemandPid = onDemand.processId(); QVERIFY(onDemandPid > 0);
        const auto ownerPath = QDir(container).filePath(".society-runtime/sync/owner.lock");
        const auto owner = [&] {
            QLockFile lock(ownerPath); qint64 pid = 0; QString host, name;
            return lock.getLockInfo(&pid, &host, &name) ? pid : qint64(0);
        };
        QTRY_COMPARE_WITH_TIMEOUT(owner(), onDemandPid, 10000);
        QCOMPARE(onDemand.state(), QProcess::Running);
        login.start(QStringLiteral(SOCIETY_DAEMON_EXECUTABLE), {"--sync"});
        QVERIFY(login.waitForStarted(5000));
        const auto loginPid = login.processId(); QVERIFY(loginPid > 0);
        QTest::qWait(500);
        QCOMPARE(login.state(), QProcess::Running);
        QCOMPARE(owner(), onDemandPid);
        onDemand.terminate();
        QVERIFY(onDemand.waitForFinished(10000));
        QTRY_COMPARE_WITH_TIMEOUT(owner(), loginPid, 15000);
        QCOMPARE(login.state(), QProcess::Running);
    }
};
QTEST_GUILESS_MAIN(BackendDaemonTests)
#include "tst_backend_daemon.moc"
