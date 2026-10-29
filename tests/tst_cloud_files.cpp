#include "../App/Society/Drive/CloudFilesController.h"
#include "AccountServer.h"
#include <SocietyDrive.h>
#include <File.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstring>
#include <algorithm>

class TransferReply final : public QNetworkReply {
public:
    TransferReply(QNetworkRequest request, QByteArray data, QObject* parent) : QNetworkReply(parent), bytes(std::move(data)) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { emit readyRead(); setFinished(true); emit finished(); });
    }
    void abort() override { setError(OperationCanceledError, "Cancelled"); }
    qint64 bytesAvailable() const override { return bytes.size() - offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* destination, qint64 maximum) override {
        const auto count = qMin(maximum, bytes.size() - offset);
        if (count <= 0) return -1;
        std::memcpy(destination, bytes.constData() + offset, std::size_t(count)); offset += count; return count;
    }
private:
    QByteArray bytes;
    qint64 offset = 0;
};
class Transfers final : public QNetworkAccessManager {
public:
    QByteArray downloaded = "cloud fixture bytes";
    QList<QByteArray> uploaded;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* outgoing) override {
        requests.append(request);
        if (operation == PutOperation) uploaded.append(outgoing->readAll());
        return new TransferReply(request, operation == GetOperation ? downloaded : QByteArray{}, this);
    }
};
class CloudFilesTests : public QObject {
    Q_OBJECT
private slots:
    void rejectsUnsafePathsAndTransferOrigins() {
        for (const auto& path : {QString{}, QString("../private"), QString("a//b"), QString("/absolute"), QString("a\\b"), QString("a/./b"), QString("C:/private")})
            QVERIFY(!CloudFilesController::validRelativePath(path));
        QVERIFY(CloudFilesController::validRelativePath("Documents/안녕.txt"));
        QVERIFY(!CloudFilesController::validRelativePath(QString::fromUtf8("e\xcc\x81.txt")));
        QVERIFY(CloudFilesController::validS3Url(QUrl("https://fixture.s3.ap-northeast-2.amazonaws.com/key?signature=fixture")));
        for (const auto& url : {"http://fixture.s3.ap-northeast-2.amazonaws.com/key", "https://foreign.invalid/key", "https://fixture.s3.amazonaws.com.foreign.invalid/key", "https://user@fixture.s3.amazonaws.com/key", "https://fixture.s3.amazonaws.com:444/key"})
            QVERIFY(!CloudFilesController::validS3Url(QUrl(url)));
    }
    void explicitTransfersStayInFilesAndKeepLocalContent() {
        AccountServer server; QVERIFY(server.server.listen(QHostAddress::LocalHost));
        iisacc::accounts::AccountManager account(server.url());
        account.setAutomaticSessionRefresh(false); account.setContainerDriveSyncEnabled(false);
        QVERIFY(account.setDeviceInfo({{"id", QString(64, 'a')}, {"type", "pc"}, {"name", "Cloud fixture"},
            {"platform", "macos"}, {"osVersion", "27"}, {"appId", "com.iisacc.society"}, {"appVersion", "1"}}));
        QVERIFY(account.loginWithPassword("fixture@example.invalid", "FixtureOnly1!"));
        QTRY_VERIFY(account.isAuthenticated());
        QTemporaryDir directory(QString(QT_TESTCASE_BUILDDIR) + "/cloud-files-XXXXXX.societycontainer");
        QVERIFY(directory.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(directory.path()));
        const auto local = directory.filePath("Files/local.bin");
        const QByteArray payload(8 * 1024 * 1024 + 17, 'x'); iiFileProvider::File::create(local, payload);
        Transfers transfers;
        CloudFilesController cloud(&transfers, nullptr); cloud.setAccountManager(&account); cloud.setContainerPath(directory.path());
        server.cloudFiles = [&](const QJsonObject& input) -> QJsonObject {
            const auto operation = input.value("operation").toString();
            if (operation == "status") return {{"quotaBytes", 5'000'000'000.0}, {"usedBytes", 0}, {"reservedBytes", 0}, {"uploads", QJsonArray{}}};
            if (operation == "list") return {{"files", QJsonArray{}}, {"quota", QJsonObject{{"quotaBytes", 5'000'000'000.0}}}};
            if (operation == "begin") return {{"state", "uploading"}, {"partBytes", 8 * 1024 * 1024}};
            if (operation == "part") {
                const auto size = input.value("partNumber").toInt() == 1 ? 8 * 1024 * 1024 : 17;
                return {{"url", "https://fixture.s3.ap-northeast-2.amazonaws.com/part"}, {"size", size},
                    {"headers", QJsonObject{{"content-length", QString::number(size)}, {"x-amz-checksum-sha256", input.value("checksumSHA256")}}}};
            }
            if (operation == "complete") return {{"quota", QJsonObject{{"quotaBytes", 5'000'000'000.0}, {"usedBytes", double(payload.size())}}}};
            if (operation == "download") return {{"url", "https://fixture.s3.ap-northeast-2.amazonaws.com/download?versionId=fixture"},
                {"file", QJsonObject{{"size", transfers.downloaded.size()}}}};
            return {};
        };
        QVERIFY(cloud.refresh()); QTRY_VERIFY(!cloud.busy()); QCOMPARE(cloud.quotaBytes(), 5'000'000'000.0);
        QVERIFY(!cloud.uploadFile("../private")); QVERIFY(!cloud.busy());
        QVERIFY(cloud.uploadFile("local.bin")); QTRY_VERIFY_WITH_TIMEOUT(!cloud.busy(), 20000);
        QCOMPARE(transfers.uploaded.size(), 2); QCOMPARE(transfers.uploaded[0] + transfers.uploaded[1], payload);
        for (const auto& request : transfers.requests) {
            QVERIFY(!request.hasRawHeader("Authorization")); QVERIFY(!request.hasRawHeader("Cookie")); QVERIFY(!request.hasRawHeader("Origin"));
        }
        const auto parts = std::count_if(server.cloudRequests.cbegin(), server.cloudRequests.cend(), [](const QJsonObject& row) { return row.value("operation") == "part"; });
        QCOMPARE(parts, 2);
        QVERIFY(!cloud.downloadFile("local.bin")); QCOMPARE(iiFileProvider::File::read(local), payload);
        QSignalSpy downloaded(&cloud, &CloudFilesController::downloaded);
        QVERIFY(cloud.downloadFile("from-cloud.bin")); QTRY_VERIFY_WITH_TIMEOUT(!cloud.busy(), 30000); QCOMPARE(downloaded.size(), 1);
        QCOMPARE(iiFileProvider::File::read(directory.filePath("Files/from-cloud.bin")), transfers.downloaded);
        account.clear(); QVERIFY(!cloud.refresh()); QVERIFY(!cloud.busy()); QVERIFY(cloud.files().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CloudFilesTests)
#include "tst_cloud_files.moc"
