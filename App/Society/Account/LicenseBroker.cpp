#include "AccountController.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QRegularExpression>
#include <QDateTime>
namespace {
void respond(QTcpSocket* socket,const QJsonObject& value,int status=200) {
    if(!socket||socket->property("responded").toBool())return;
    socket->setProperty("responded",true);const auto body=QJsonDocument(value).toJson(QJsonDocument::Compact);
    socket->write("HTTP/1.1 "+QByteArray::number(status)+" Response\r\nContent-Type: application/json\r\nCache-Control: private, no-store\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);socket->disconnectFromHost();
}
}
QUrl AccountController::licenseBrokerUrl()const{return QUrl(QString("http://127.0.0.1:%1/v1/authorize").arg(licenseBroker_.serverPort()));}
bool AccountController::startLicenseBroker(quint16 port) {
    if(licenseBroker_.isListening())return true;
    if(!licenseBroker_.listen(QHostAddress::LocalHost,port))return false;
    licenseBroker_.setMaxPendingConnections(16);
    connect(&licenseBroker_,&QTcpServer::newConnection,this,[this]{
        while(auto* socket=licenseBroker_.nextPendingConnection()) {
            if(brokerRequests_>=16){socket->disconnectFromHost();socket->deleteLater();continue;}++brokerRequests_;
            connect(socket,&QTcpSocket::disconnected,this,[this,socket]{--brokerRequests_;socket->deleteLater();});
            socket->setReadBufferSize(8193);
            QTimer::singleShot(12000,socket,[socket]{respond(socket,{{"allowed",false},{"code","request_timeout"}},408);});
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
                if(socket->property("handled").toBool())return;
                auto bytes=socket->property("bytes").toByteArray()+socket->readAll();socket->setProperty("bytes",bytes);
                if(bytes.size()>8192){respond(socket,{{"allowed",false}},413);return;}
                const auto split=bytes.indexOf("\r\n\r\n");if(split<0)return;
                const auto lines=bytes.left(split).split('\n');int length=-1;bool valid=lines.first().trimmed()=="POST /v1/authorize HTTP/1.1";bool host=false,content=false;
                for(auto line:lines.mid(1)) {
                    const auto key=line.left(line.indexOf(':')).trimmed().toLower();const auto value=line.mid(line.indexOf(':')+1).trimmed();
                    if(key=="origin"||key=="transfer-encoding")valid=false;
                    if(key=="host"){valid=valid&&!host&&value==QByteArray("127.0.0.1:")+QByteArray::number(licenseBroker_.serverPort());host=true;}
                    if(key=="content-type"){valid=valid&&!content&&value=="application/json";content=true;}
                    if(key=="content-length"){bool ok=false;int n=value.toInt(&ok);valid=valid&&length==-1&&ok&&n>0&&n<=4096;length=n;}
                }
                if(!valid||!host||!content||length<1){respond(socket,{{"allowed",false}},400);return;}
                if(bytes.size()<split+4+length)return;
                socket->setProperty("handled",true);
                QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes.mid(split+4,length),&error);
                if(error.error!=QJsonParseError::NoError||!document.isObject()){respond(socket,{{"allowed",false}},400);return;}
                authorizeApp(socket,document.object());
            });
        }
    });
    if(signedIn())manager()->licenseManager()->ensure();
    return true;
}
void AccountController::authorizeApp(QTcpSocket* socket,const QJsonObject& input) {
    const QHash<QString,QString> apps{{"congregation","com.iisacc.congregation.painter"},{"dreamscapes","com.iisacc.dreamscapes"},{"vincent","com.iisacc.vincent"}};
    static const QRegularExpression nonce("\\A[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}\\z");
    const auto product=input.value("productId").toString(),id=input.value("requestId").toString();
    if(!apps.contains(product)||input.value("appId")!=apps.value(product)||!nonce.match(id).hasMatch()){respond(socket,{{"allowed",false},{"code","invalid_app_request"}},400);return;}
    const auto binding=manager()->sessionBinding();
    if(!signedIn()||input.value("subject").toString()!=binding.value("subject").toString()||input.value("deviceId").toString()!=manager()->deviceInfo().value("id").toString()) {
        respond(socket,{{"allowed",false},{"code","society_account_required"},{"requestId",id}});return;
    }
    auto* licenses=manager()->licenseManager();
    auto connection=std::make_shared<QMetaObject::Connection>();
    auto finish=[this,socket=QPointer<QTcpSocket>(socket),input,product,id,binding,connection]{
        if(!socket){disconnect(*connection);return;}
        auto* licenses=manager()->licenseManager();if(licenses->busy())return;
        disconnect(*connection);
        const bool current=manager()->matchesSessionBinding(binding);
        const auto token=current?licenses->authorizationToken():QByteArray{};
        const bool allowed=current&&licenses->allows(product);
        respond(socket,{{"allowed",allowed},{"requestId",id},{"licenseToken",QString::fromLatin1(token)},
            {"code",token.isEmpty()?licenses->code():allowed?"licensed":"license_not_owned"},{"source",licenses->code()},{"expiresAt",current?licenses->expiresAt():0}});
    };
    *connection=connect(licenses,&iisacc::accounts::LicenseManager::changed,socket,finish);
    if(input.value("refresh").toBool())licenses->refresh();else licenses->ensure();
    finish();
}
