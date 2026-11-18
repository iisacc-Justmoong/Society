#include "Society/Native/NativePhotoPresenter.h"
#include "Society/Native/NativeModalPresence.h"
#include "Society/Native/OverlayShutdown.h"
#include <iiPhotoLibrary>
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void events(){for(int i=0;i<4;++i){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}
class FakeLibrary final : public iiPhotoLibrary::PhotoLibrary {
public:
    QString name()const override{return "Photo presenter fixture";}
    iiPhotoLibrary::Access access()const override{return iiPhotoLibrary::Access::Full;}
    void authorize(std::function<void()> done)override{done();}
    iiPhotoLibrary::Snapshot scan(QString*)override{return {};}
    bool preview(const QString&,const QString&,QString*)override{return false;}
    bool exportResource(const QString&,const QString&,const QString&,QString*)override{return false;}
    QString import(const QJsonObject&,const QStringList&,QString*)override{return {};}
    bool trash(const QString&,QString*)override{return false;}
};
class FakeLoader final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem* item READ item WRITE setItem NOTIFY itemChanged)
public:
    using QObject::QObject;
    QQuickItem* item()const{return item_;}
    void setItem(QQuickItem* value){if(item_==value)return;item_=value;emit itemChanged();}
signals: void itemChanged();
private: QPointer<QQuickItem> item_;
};
class FakeStorage final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* photos READ photos WRITE setPhotos NOTIFY photosChanged)
public:
    using QQuickItem::QQuickItem;
    QObject* photos()const{return photos_;}
    void setPhotos(QObject* value){if(photos_==value)return;photos_=value;emit photosChanged();}
signals: void photosChanged();
private: QPointer<QObject> photos_;
};
class FakeLegacy final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    using QQuickItem::QQuickItem;
    QObject* controller()const{return controller_;}
    void setController(QObject* value){if(controller_==value)return;controller_=value;emit controllerChanged();}
signals: void controllerChanged();
private: QPointer<QObject> controller_;
};
class FakeSurface final : public society::native::PhotoSurface {
public:
    FakeSurface(society::native::PhotoPredicate wanted,society::native::PhotoPredicate ready,std::function<void()> retired)
        :wanted_(std::move(wanted)),ready_(std::move(ready)),retired_(std::move(retired)){}
    bool start(std::function<void(bool)> done)override{
        if(!wanted_() || !ready_())return false;
        done_=std::move(done);started_=true;return true;
    }
    void requestClose()noexcept override{
        if(!started_)return;started_=false;auto done=std::move(done_);if(retired_)retired_();if(done)done(false);
    }
    bool update(const society::native::PhotoArea&)noexcept override{return true;}
private:
    society::native::PhotoPredicate wanted_,ready_;
    std::function<void()> retired_;
    std::function<void(bool)> done_;
    bool started_=false;
};
}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);society::native::OverlayShutdown shutdown;QObject lifetime,invalid;
    QQuickWindow window;auto* overlay=new QQuickItem(window.contentItem());
    using society::native::installNativePhotoPresenter;
    check(!installNativePhotoPresenter(&invalid,&lifetime,&shutdown,overlay),"A non-window photo host was admitted");
    check(!installNativePhotoPresenter(&window,&lifetime,&shutdown,overlay),"A missing storage loader was admitted");
    auto* loader=new FakeLoader(&window);loader->setObjectName("storageLoader");
    int surfaces=0;
    society::native::PhotoSurfaceFactory factory=[&](QWindow*,society::native::PhotoArea,society::native::PhotoPredicate wanted,
        society::native::PhotoMount,society::native::PhotoPredicate ready,std::function<void()> retired){
        ++surfaces;return std::make_shared<FakeSurface>(std::move(wanted),std::move(ready),std::move(retired));
    };
    check(installNativePhotoPresenter(&window,&lifetime,&shutdown,overlay,factory),"A valid hidden photo host was rejected");
    auto controller=std::make_unique<iiPhotoLibrary::PhotoController>(iiPhotoLibrary::PhotoController::Sender{},nullptr,std::make_shared<FakeLibrary>());
    auto* storage=new FakeStorage(window.contentItem());storage->setObjectName("storageView");storage->setPhotos(controller.get());
    auto* legacy=new FakeLegacy(storage);legacy->setObjectName("photosView");legacy->setController(controller.get());
    loader->setItem(storage);
    check(loader->property("item").value<QObject*>()==storage,"The loader item is not available as an object property");
    check(storage->property("photos").value<QObject*>()==controller.get(),"The storage photo service is not available as an object property");
    check(storage->findChild<QQuickItem*>(QStringLiteral("photosView"))==legacy,"The photo item is not an object child of the storage view");
    events();
    QPointer<QObject> firstSession=lifetime.findChild<QObject*>(QStringLiteral("societyNativePhotoSession"));
    check(firstSession,"The existing photo service was not connected to a native session");
    check(!window.isVisible() && legacy->controller()==controller.get() && legacy->opacity()==1 && legacy->isEnabled(),
          "A hidden photo host changed the legacy view or showed a window");
    controller.reset();storage->setPhotos(nullptr);events();
    check(!firstSession,"The previous photo session survived service retirement");
    auto next=std::make_unique<iiPhotoLibrary::PhotoController>(iiPhotoLibrary::PhotoController::Sender{},nullptr,std::make_shared<FakeLibrary>());
    storage->setPhotos(next.get());legacy->setController(next.get());events();
    check(lifetime.findChild<QObject*>(QStringLiteral("societyNativePhotoSession")),"A replacement photo service did not get a new session");
    check(legacy->controller()==next.get(),"Replacing a hidden photo service lost the legacy binding");
    window.resize(640,400);storage->setWidth(400);storage->setHeight(300);legacy->setWidth(400);legacy->setHeight(300);
    window.show();events();
    check(surfaces==1 && !legacy->controller() && !legacy->isEnabled() && legacy->opacity()==0,
          "An eligible photo view did not transfer to the native surface");
    auto modal=society::native::NativeModalPresence::forWindow(&window)->acquire();events();
    check(legacy->controller()==next.get() && legacy->isEnabled() && legacy->opacity()==1,
          "Opening a native modal did not restore the legacy view beneath it");
    modal.reset();events();
    check(surfaces==2 && !legacy->controller() && legacy->opacity()==0,"Closing the modal did not restore the native photo surface");
    auto third=std::make_unique<iiPhotoLibrary::PhotoController>(iiPhotoLibrary::PhotoController::Sender{},nullptr,std::make_shared<FakeLibrary>());
    storage->setPhotos(third.get());events();
    check(surfaces==3 && !legacy->controller(),"Replacing the active photo service did not rebind the native view");
    window.hide();events();
    check(legacy->controller()==third.get() && legacy->isEnabled() && legacy->opacity()==1,
          "Hiding the photo view did not restore the current legacy service");
    storage->setPhotos(next.get());events();
    check(legacy->controller()==next.get(),"A detached legacy binding did not follow the replacement service");
    loader->setItem(nullptr);delete storage;events();
    // Exercise the actual `property var` binding shape used by StorageView,
    // rather than assuming it behaves like a typed C++ QObject property.
    QQmlEngine qml;
    QQmlComponent component(&qml);
    component.setData(R"(import QtQuick
Item {
    id: root
    width: 400; height: 300
    property var photos: null
    Item { objectName: "photosView"; anchors.fill: parent; property var controller: root.photos }
})",QUrl());
    auto* qmlStorage=qobject_cast<QQuickItem*>(component.create());
    check(qmlStorage,"The QML property binding fixture could not be created");
    qmlStorage->setParentItem(window.contentItem());
    qmlStorage->setProperty("photos",QVariant::fromValue(static_cast<QObject*>(next.get())));
    auto* qmlLegacy=qmlStorage->findChild<QQuickItem*>(QStringLiteral("photosView"));
    check(qmlLegacy && qmlLegacy->property("controller").value<QObject*>()==next.get(),"The QML service binding was not established");
    loader->setItem(qmlStorage);window.show();events();
    check(!qmlLegacy->property("controller").value<QObject*>() && !qmlLegacy->isEnabled() && qmlLegacy->opacity()==0,
          "The native presenter did not detach the real QML var binding");
    qmlStorage->setProperty("photos",QVariant::fromValue(static_cast<QObject*>(third.get())));events();
    check(!qmlLegacy->property("controller").value<QObject*>(),"The replacement QML service did not mount natively");
    window.hide();events();
    check(qmlLegacy->property("controller").value<QObject*>()==third.get(),"The QML service binding was not restored on hide");
    loader->setItem(nullptr);delete qmlStorage;events();
    QQmlComponent windowComponent(&qml);
    windowComponent.setData(R"(import QtQuick
import QtQuick.Controls as Controls
Controls.ApplicationWindow {
    id: root
    width: 640; height: 400
    property var photos: null
    Item { parent: Controls.Overlay.overlay; objectName: "emptyOverlayHost" }
    Controls.Popup { objectName: "fixturePopup"; parent: Controls.Overlay.overlay; modal: true; width: 200; height: 100; enter: null; exit: null }
    Loader {
        objectName: "storageLoader"; anchors.fill: parent
        sourceComponent: Item {
            property var photos: root.photos
            Item { objectName: "photosView"; anchors.fill: parent; property var controller: parent.photos }
        }
    }
})",QUrl());
    auto* controlsWindow=qobject_cast<QQuickWindow*>(windowComponent.create());
    check(controlsWindow,"The Qt Controls overlay fixture could not be created");
    controlsWindow->setProperty("photos",QVariant::fromValue(static_cast<QObject*>(next.get())));
    auto* controlsLoader=controlsWindow->findChild<QObject*>(QStringLiteral("storageLoader"));
    check(controlsLoader,"The Qt Controls storage loader was not created");
    auto* controlsStorage=qobject_cast<QQuickItem*>(controlsLoader->property("item").value<QObject*>());
    auto* controlsLegacy=controlsStorage?controlsStorage->findChild<QQuickItem*>(QStringLiteral("photosView")):nullptr;
    auto* popup=controlsWindow->findChild<QObject*>(QStringLiteral("fixturePopup"));
    check(controlsLegacy && popup,"The Qt Controls photo or popup fixture is missing");
    check(installNativePhotoPresenter(controlsWindow,&lifetime,&shutdown,nullptr,factory),"The Qt Controls overlay was not discovered");
    controlsWindow->show();events();
    check(!controlsLegacy->property("controller").value<QObject*>(),"An empty Qt overlay host prevented the native photo surface");
    popup->setProperty("visible",true);events();
    check(controlsLegacy->property("controller").value<QObject*>()==next.get(),"A Qt popup did not restore the legacy view beneath it");
    popup->setProperty("visible",false);events();
    check(!controlsLegacy->property("controller").value<QObject*>(),"Closing the Qt popup did not remount the photo view");
    delete controlsWindow;events();
    QEvent quit(QEvent::Quit);QCoreApplication::sendEvent(&app,&quit);events();
    check(shutdown.isClosing(),"Photo presenter did not join application shutdown");
}
#include "presenter.moc"
