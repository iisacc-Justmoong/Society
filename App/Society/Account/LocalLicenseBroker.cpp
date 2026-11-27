#include "AccountController.h"
#include <iiSocietyHelper>
#include <QLocalSocket>
#include <cerrno>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QPointer>
#include <QNetworkReply>
#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace {
#if defined(Q_OS_UNIX)
QString brokerDirectory(const QString &directory) {
    const auto configured=qEnvironmentVariable("SOCIETY_LOCAL_BROKER_DIR");
    return !directory.isEmpty()?directory:!configured.isEmpty()?configured:QDir::homePath()+"/.iisacc-society-ipc-v1";
}
bool prepareBrokerDirectory(const QString &base) {
    if(!QDir::isAbsolutePath(base))return false;
    const auto encoded=QFile::encodeName(base);struct stat dir{};
    if(::mkdir(encoded.constData(),0700)<0 && errno!=EEXIST)return false;
    return ::lstat(encoded.constData(),&dir)==0 && S_ISDIR(dir.st_mode) && dir.st_uid==::geteuid() && !(dir.st_mode&0077);
}
bool currentUser(QLocalSocket* socket) {
#if defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD)
    uid_t user{};gid_t group{};return ::getpeereid(int(socket->socketDescriptor()),&user,&group)==0 && user==::geteuid();
#elif defined(Q_OS_LINUX)
    struct ucred peer{};socklen_t length=sizeof(peer);
    return ::getsockopt(int(socket->socketDescriptor()),SOL_SOCKET,SO_PEERCRED,&peer,&length)==0 && peer.uid==::geteuid();
#else
    (void)socket;return false;
#endif
}
#endif
void respond(QLocalSocket* socket, const QJsonObject& value) {
    if(!socket||socket->property("responded").toBool())return;
    socket->setProperty("responded",true);
    const auto body=QJsonDocument(value).toJson(QJsonDocument::Compact);const auto n=quint32(body.size());
    QByteArray header;header.append(char(n>>24));header.append(char(n>>16));header.append(char(n>>8));header.append(char(n));
    socket->write(header+body);socket->disconnectFromServer();
}
}
bool AccountController::startLocalHelperBroker(const QString &directory) {
#if defined(Q_OS_UNIX)
    if (localHelper_) return true;
    const auto base = brokerDirectory(directory);
    if (!prepareBrokerDirectory(base)) return false;
    auto helper = std::make_unique<iiSocietyHelper::LocalService>(this, [this](const QString &product, const QString &app) {
        static const QHash<QString, QString> apps{{"congregation", "com.iisacc.congregation.painter"},
            {"dreamscapes", "com.iisacc.dreamscapes"}};
        return apps.contains(product) && apps.value(product) == app && signedIn()
            && manager()->licenseManager()->allows(product);
    });
    if (!helper->listen(base)) return false;
    localHelper_ = std::move(helper);
    if (signedIn()) manager()->licenseManager()->ensure();
    return true;
#else
    (void)directory;return false;
#endif
}
bool AccountController::startLocalLicenseBroker(const QString &directory, bool startHelper) {
#if defined(Q_OS_UNIX)
    if(localBroker_.isListening())return true;
    const auto base=brokerDirectory(directory);
    if(!prepareBrokerDirectory(base))return false;
    const auto lockPath=QFile::encodeName(base+"/broker.lock");
    const int lock=::open(lockPath.constData(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);struct stat lockStat{};
    if(lock<0)return false;
    if(::fstat(lock,&lockStat)<0 || !S_ISREG(lockStat.st_mode) || lockStat.st_uid!=::geteuid() || (lockStat.st_mode&0077) || ::flock(lock,LOCK_EX|LOCK_NB)<0){::close(lock);return false;}
    const auto path=base+"/broker.sock";const auto encodedPath=QFile::encodeName(path);struct stat existing{};
    if(::lstat(encodedPath.constData(),&existing)==0) {
        if(!S_ISSOCK(existing.st_mode)||existing.st_uid!=::geteuid()||(existing.st_mode&0077)){::close(lock);return false;}
        QLocalSocket probe;probe.connectToServer(path);if(probe.waitForConnected(100)){probe.disconnectFromServer();::close(lock);return false;}
        if(::unlink(encodedPath.constData())<0){::close(lock);return false;}
    } else if(errno!=ENOENT){::close(lock);return false;}
    localBroker_.setSocketOptions(QLocalServer::UserAccessOption);
    if(!localBroker_.listen(path)){::close(lock);return false;}
    struct stat sock{};
    if(::lstat(encodedPath.constData(),&sock)<0 || !S_ISSOCK(sock.st_mode)||sock.st_uid!=::geteuid()||::chmod(encodedPath.constData(),0600)<0){localBroker_.close();::close(lock);return false;}
    localBrokerLock_=lock;
    if (startHelper && !startLocalHelperBroker(base)) { stopLocalLicenseBroker(); return false; }
    connect(&localBroker_,&QLocalServer::newConnection,this,[this]{
        while(auto* socket=localBroker_.nextPendingConnection()) {
            if(!currentUser(socket)||localRequests_>=16){socket->disconnectFromServer();socket->deleteLater();continue;}
            ++localRequests_;
            socket->setReadBufferSize(65540);
            connect(socket,&QObject::destroyed,this,[this]{--localRequests_;});
            connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            QTimer::singleShot(10000,socket,[socket]{if(socket->state()!=QLocalSocket::UnconnectedState)socket->abort();});
            connect(socket,&QLocalSocket::readyRead,this,[this,socket]{
                if(socket->property("handled").toBool())return;
                auto bytes=socket->property("bytes").toByteArray()+socket->readAll();socket->setProperty("bytes",bytes);
                if(bytes.size()<4)return;
                const auto* b=reinterpret_cast<const unsigned char*>(bytes.constData());
                const quint32 n=(quint32(b[0])<<24)|(quint32(b[1])<<16)|(quint32(b[2])<<8)|quint32(b[3]);
                if(n==0||n>4096||bytes.size()>int(n+4)){socket->abort();return;}
                if(bytes.size()<int(n+4))return;
                socket->setProperty("handled",true);
                QJsonParseError error;const auto parsed=QJsonDocument::fromJson(bytes.mid(4),&error);
                if(error.error!=QJsonParseError::NoError||!parsed.isObject()){socket->abort();return;}
                const auto input=parsed.object();const auto product=input.value("productId").toString();const auto requestId=input.value("requestId").toString();
                const auto operation=input.value("operation").toString();
                static const QHash<QString,QString> apps{{"congregation","com.iisacc.congregation.painter"},{"dreamscapes","com.iisacc.dreamscapes"}};
                static const QRegularExpression nonce("\\A[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}\\z");
                if(!apps.contains(product)||input.value("appId")!=apps.value(product)||!nonce.match(requestId).hasMatch()
                    || (!operation.isEmpty() && operation!="local-license-v2" && operation!="update")) { respond(socket,{{"allowed",false},{"code","invalid_app_request"},{"requestId",requestId}});return; }
                QTimer::singleShot(750,socket,[socket,requestId]{respond(socket,{{"code","verification_unavailable"},{"requestId",requestId}});});
                withBrokerAccount(socket, [broker=QPointer<AccountController>(this),socket=QPointer<QLocalSocket>(socket),input,product,requestId](AccountController *account) {
                    if (!socket) return;
                    if (!account || !account->signedIn() || input.value("deviceId").toString()!=account->manager()->deviceInfo().value("id").toString()) {
                        respond(socket,{{"allowed",false},{"code","society_account_required"},{"requestId",requestId}});return;
                    }
                    const QPointer<AccountController> currentAccount(account);
                    const auto binding=account->manager()->sessionBinding();
                    auto* licenses=account->manager()->licenseManager();
                    auto connection=std::make_shared<QMetaObject::Connection>();
                    auto completed=std::make_shared<bool>(false);
                    auto finish=[broker,socket,currentAccount,input,product,requestId,binding,connection,completed] {
                        if(!socket || !currentAccount){QObject::disconnect(*connection);return;}
                        if(*completed)return;
                        auto* manager=currentAccount->manager();auto* checked=manager->licenseManager();if(checked->busy())return;
                        *completed=true;
                        QObject::disconnect(*connection);
                        const bool current=broker && broker->brokerAccountIsCurrent(currentAccount)
                            && manager->matchesSessionBinding(binding);
                        if (!current) {
                            respond(socket,{{"allowed",false},{"code","society_account_required"},{"requestId",requestId}});return;
                        }
                        const auto token=checked->authorizationToken();
                        const bool allowed=checked->allows(product);
                        if (input.value("operation") != "update") {
                            if (input.value("operation") == "local-license-v2") {
                                respond(socket,{{"requestId",requestId},{"localToken",QString::fromLatin1(token)},
                                    {"code",token.isEmpty()?checked->code():"local_token"}});return;
                            }
                            respond(socket,{{"allowed",allowed},{"requestId",requestId},{"licenseToken",QString::fromLatin1(token)},
                                {"code",token.isEmpty()?checked->code():allowed?"licensed":"license_not_owned"}});return;
                        }
                        const auto action=input.value("action").toString();
                        if(!allowed || (action!="check" && action!="grant")) {
                            respond(socket,{{"code","update_not_authorized"},{"requestId",requestId}});return;
                        }
                        // Forward only the bounded, paired incremental grant capability.
                        const bool hasDelta=input.contains("deltaFrom") || input.contains("deltaFormat");
                        const auto base=input.value("deltaFrom").toString();
                        static const QRegularExpression version("\\A(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\z");
                        bool validBase=base.size()<=32 && version.match(base).hasMatch();
                        for(const auto &component:base.split('.')) {
                            bool ok=false;const auto number=component.toULongLong(&ok);
                            if(!ok||number>2147483647ULL)validBase=false;
                        }
                        if(hasDelta && (action!="grant" || !validBase || input.value("deltaFormat")!="iiudlt1")) {
                            respond(socket,{{"code","invalid_update_delta"},{"requestId",requestId}});return;
                        }
                        QJsonObject request{{"productId",product},{"deviceId",input.value("deviceId")},
                            {"platformKey",input.value("platformKey")},{"action",action},{"requestId",requestId},
                            {"currentVersion",input.value("currentVersion")},{"version",input.value("version")}};
                        if(hasDelta) {request.insert("deltaFrom",base);request.insert("deltaFormat","iiudlt1");}
                        const auto body=QJsonDocument(QJsonObject{{"query","mutation AccountUpdates($input: JSON!) { accountUpdates(input: $input) }"},
                            {"variables",QJsonObject{{"input",request}}}}).toJson(QJsonDocument::Compact);
                        auto* reply=manager->postAuthenticated(manager->serviceUrl().resolved(QUrl("/Account/GraphQL")),body);
                        if(!reply){respond(socket,{{"code","update_unavailable"},{"requestId",requestId}});return;}
                        reply->setReadBufferSize(65537);
                        QObject::connect(reply,&QNetworkReply::finished,reply,&QObject::deleteLater);
                        QObject::connect(socket.data(),&QObject::destroyed,reply,&QNetworkReply::abort);
                        QObject::connect(reply,&QIODevice::readyRead,socket.data(),[reply] { if(reply->bytesAvailable()>65536)reply->abort(); });
                        QObject::connect(reply,&QNetworkReply::finished,socket.data(),[broker,currentAccount,reply,socket,requestId,binding,product] {
                            const auto data=reply->readAll();const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            QJsonParseError error;const auto root=QJsonDocument::fromJson(data,&error).object();
                            const auto result=root.value("data").toObject().value("accountUpdates").toObject();
                            const bool ok=currentAccount && reply->error()==QNetworkReply::NoError && status==200 && data.size()<=65536 &&
                                error.error==QJsonParseError::NoError && !root.contains("errors") && !result.isEmpty() &&
                                reply->header(QNetworkRequest::ContentTypeHeader).toString().split(';').first().trimmed()=="application/json" &&
                                broker && broker->brokerAccountIsCurrent(currentAccount) &&
                                currentAccount->manager()->matchesSessionBinding(binding) && currentAccount->manager()->licenseManager()->allows(product);
                            if(ok)respond(socket,{{"requestId",requestId},{"result",result}});
                            else respond(socket,{{"requestId",requestId},{"code","update_unavailable"}});
                        });
                    };
                    *connection=QObject::connect(licenses,&iisacc::accounts::LicenseManager::changed,socket.data(),finish);
                    if(input.value("refresh").toBool())licenses->refresh();else licenses->ensure();finish();
                });
            });
        }
    });
    if(signedIn())manager()->licenseManager()->ensure();
    return true;
#else
    (void)directory;(void)startHelper;return false;
#endif
}
void AccountController::stopLocalLicenseBroker() {
    localHelper_.reset();
    localBroker_.close();
#if defined(Q_OS_UNIX)
    if(localBrokerLock_>=0){::close(localBrokerLock_);localBrokerLock_=-1;}
#endif
}
QString AccountController::localHelperBrokerPath() const { return localHelper_ ? localHelper_->path() : QString{}; }
