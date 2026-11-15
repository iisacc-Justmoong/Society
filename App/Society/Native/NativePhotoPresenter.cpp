#include "NativePhotoPresenter.h"
#include "ExternalPhotoSurface.h"
#include "NativeModalPresence.h"
#include "NativeOverlayCoordinator.h"
#include "OverlayShutdown.h"
#include "PhotoAreaTracker.h"
#include "PhotoPicker.h"
#include "PhotoPreviewLoader.h"
#include "PhotoPreviewResolver.h"
#include "PhotosView.generated.h"
#include <iiGUI/Host.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <iiPhotoLibrary/PhotoPresentationBridge.h>
#include <iiPhotoLibrary/PhotoViewModel.h>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <memory>
#include <stdexcept>
#include <utility>

namespace society::native {
namespace {
namespace photos=iiPhotoLibrary::presentation;
using View=lvui_generated::PhotosView;
using Model=photos::PhotoViewModel;

QObject* propertyObject(QObject* object,const char* name) {
    if (!object) return nullptr;
    return object->property(name).value<QObject*>();
}
bool loadCatalog(lvui::native::AppStrings& strings,const char* language) {
    const auto languageTag=QString::fromLatin1(language);
    QFile file(QStringLiteral(":/iiPhotoLibrary/native-strings/")+languageTag+QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly) || file.size()>static_cast<qint64>(lvui::native::AppStrings::maximum_catalog_bytes)) return false;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) return false;
    const auto catalog=document.object();const auto locale=catalog.value("locale").toString();
    if ((locale!=languageTag && !locale.startsWith(languageTag+QLatin1Char('-'))) || !catalog.value("strings").isObject()) return false;
    lvui::native::AppStrings::Entries entries;const auto values=catalog.value("strings").toObject();
    for (auto it=values.begin();it!=values.end();++it) {
        if (!it.value().isString()) return false;
        entries.emplace(it.key().toStdString(),it.value().toString().toStdString());
    }
    // A region-specific catalog also supplies the language fallback, so a
    // language-only system locale does not silently fall back to English.
    if (locale!=languageTag && !strings.replace_catalog(language,entries)) return false;
    return bool(strings.replace_catalog(locale.toStdString(),std::move(entries)));
}

class PhotoSession final : public QObject {
public:
    PhotoSession(QQuickWindow* window,QQuickItem* storage,QQuickItem* legacy,iiPhotoLibrary::PhotoController* controller,
                 QQuickItem* overlay,NativeModalPresence* modal,PhotoSurfaceFactory factory,
                 std::function<void()> invalidated,std::function<void()> bindingDetached,QObject* parent)
        :QObject(parent),window_(window),storage_(storage),legacy_(legacy),overlay_(overlay),controller_(controller),modal_(modal),
         factory_(std::move(factory)),invalidated_(std::move(invalidated)),bindingDetached_(std::move(bindingDetached)),
         model_(std::make_shared<Model>()),strings_(std::make_shared<lvui::native::AppStrings>()) {
        setObjectName(QStringLiteral("societyNativePhotoSession"));
        if (!loadCatalog(*strings_,"en") || !loadCatalog(*strings_,"ko")) throw std::runtime_error("Cannot load photo view strings");
        translate();model_->hide();
        // The cache bound is also the maximum number of unique outstanding
        // requests. The loader can admit the same total without dropping a
        // visible preview, and starts only two decoder tasks at a time.
        constexpr std::size_t cacheEntries=256, activeDecodes=2;
        loader_=std::make_unique<PhotoPreviewLoader>(PhotoPreviewLoader::Limits{
            .sourceBytes=32ull*1024*1024,.sourcePixels=64ull*1024*1024,
            .maxInFlight=activeDecodes,.maxQueued=cacheEntries-activeDecodes});
        const QPointer<PhotoPreviewLoader> loader=loader_.get();
        resolver_=std::make_shared<PhotoPreviewResolver>(PhotoPreviewResolver::Limits{.entries=cacheEntries,.bytes=64ull*1024*1024},
            [loader](std::string source,std::uint32_t width,std::uint32_t height,PhotoPreviewResolver::Completion done){
                if (loader) loader->load(std::move(source),width,height,std::move(done));else if(done)done({});
            });
        images_=std::make_shared<lvui::native::ImageServices<ii::gui::RasterImage>>();images_->resolver.set(resolver_);
        const QPointer<PhotoSession> self(this);
        bridge_=std::make_unique<photos::PhotoPresentationBridge>(controller,model_,
            [self]{if(self)self->pickFiles();},[weak=std::weak_ptr<PhotoPreviewResolver>(resolver_)](std::string_view source){
                if(auto resolver=weak.lock())resolver->invalidate(source);
            });
        coordinator_=std::make_unique<NativeOverlayCoordinator>(
            [self]()->std::shared_ptr<NativeOverlaySurface>{return self?self->createSurface():nullptr;},
            [self](NativeOverlayCoordinator::Task task){if(self)QTimer::singleShot(0,self,[task=std::move(task)]{task();});},
            [self]{if(self)self->fallback();});
        modalSubscription_=modal->count().observe([self](std::size_t,std::uint64_t){if(self)self->reconcile();});
        tracker_=std::make_unique<PhotoAreaTracker>(legacy,window,[self](const PhotoArea& area){
            if(self){self->area_=area;self->reconcile();}
        },overlay);
        const auto invalidate=[self]{
            if(!self)return;
            self->reconcile();
            if(self && self->invalidated_)self->invalidated_();
        };
        connect(controller,&QObject::destroyed,this,invalidate);
        connect(storage,&QObject::destroyed,this,invalidate);
        connect(legacy,&QObject::destroyed,this,invalidate);
        connect(legacy,&QQuickItem::enabledChanged,this,[self]{if(self && !self->suppressed_)self->reconcile();});
        connect(legacy,&QQuickItem::opacityChanged,this,[self]{if(self && !self->suppressed_)self->reconcile();});
        connect(overlay,&QObject::destroyed,this,invalidate);
        connect(modal,&QObject::destroyed,this,invalidate);
        connect(window,&QObject::destroyed,this,invalidate);
        window->installEventFilter(this);if(qApp)qApp->installEventFilter(this);
    }
    ~PhotoSession() override {shutdown({});}
    bool matches(QQuickItem* storage,QQuickItem* legacy,iiPhotoLibrary::PhotoController* controller) const {
        return !stopping_ && controller_ && modal_ && overlay_ && storage_==storage && legacy_==legacy && controller_==controller &&
            (!suppressed_ || !propertyObject(legacy_,"controller"));
    }
    bool suppressed() const noexcept{return suppressed_;}
    void shutdown(std::function<void()> done) {
        const QPointer<PhotoSession> self(this);
        const bool first=!std::exchange(stopping_,true);
        (void)picker_.cancel();
        if (coordinator_) coordinator_->shutdown(std::move(done));else if(done)done();
        if (!self || !first) return;
        tracker_.reset();modalSubscription_.reset();
        if (model_) model_->hide();
        if (!self) return;
        restoreLegacy();
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if ((watched==qApp || watched==window_) && event->type()==QEvent::LanguageChange) translate();
        return QObject::eventFilter(watched,event);
    }
private:
    void translate() {
        if (!strings_) return;
        const auto locale=QLocale().bcp47Name().toStdString();
        (void)strings_->set_locale(locale.empty()?"en":locale);
    }
    bool wanted() const {
        return !stopping_ && window_ && window_->isVisible() && storage_ && legacy_ && overlay_ && controller_ && modal_ &&
            (suppressed_ || (legacy_->isEnabled() && legacy_->opacity()>0)) &&
            area_.mode==PhotoArea::Mode::visible && modal_->count().get()==0;
    }
    void reconcile() {
        const QPointer<PhotoSession> self(this);
        const bool active=wanted();
        if (auto surface=surface_.lock()) (void)surface->update(active?area_:PhotoArea{});
        if (!self) return;
        if (!active) {
            (void)picker_.cancel();
            model_->hide();if(!self)return;
            restoreLegacy();if(!self)return;
        }
        if (coordinator_) coordinator_->setWanted(active);
    }
    std::shared_ptr<NativeOverlaySurface> createSurface() {
        if (!wanted()) return {};
        auto scope=std::make_shared<std::shared_ptr<View::Scope>>();
        const QPointer<PhotoSession> self(this);
        auto surface=factory_(window_,area_,
            [self]{return self && self->wanted();},
            [self,scope](ii::gui::Host& host){
                if (!self) return false;
                const auto model=self->model_;
                const auto strings=self->strings_;
                const auto images=self->images_;
                const auto factories=lvui::native::BackendFactories{}.with_app_strings(strings)
                    .with<photos::PhotoBackend>([model]{return photos::PhotoBackend(model);})
                    .with<std::shared_ptr<lvui::native::ImageServices<ii::gui::RasterImage>>>([images]{return images;});
                if (!View::mount(host,{},scope.get(),factories)) return false;
                if (!self) return false;
                self->host_=&host;return true;
            },
            [self]{return self && self->suppressLegacy();},
            [self,scope]{if(self)self->host_=nullptr;scope->reset();if(self)self->restoreLegacy();});
        surface_=surface;return surface;
    }
    bool suppressLegacy() {
        if (!wanted() || !legacy_ || propertyObject(legacy_,"controller")!=controller_) return false;
        if (suppressed_) return true;
        previousOpacity_=legacy_->opacity();previousEnabled_=legacy_->isEnabled();suppressed_=true;
        const QPointer<PhotoSession> self(this);const auto legacy=legacy_;const auto controller=controller_;
        if(bindingDetached_)bindingDetached_();
        if(!self || !legacy || !controller)return false;
        controller->prioritizeVisible({});
        if (!self || !legacy || !controller) return false;
        legacy->setProperty("controller",QVariant::fromValue(static_cast<QObject*>(nullptr)));
        if (!self || !legacy || propertyObject(legacy,"controller")) return false;
        legacy->setEnabled(false);
        if (!self || !legacy) return false;
        legacy->setOpacity(0);
        if (!self || !legacy) return false;
        model_->show();
        return self && wanted();
    }
    void restoreLegacy() {
        if (!std::exchange(suppressed_,false)) return;
        const auto legacy=legacy_;const auto storage=storage_;const auto controller=controller_;
        const auto opacity=previousOpacity_;const auto enabled=previousEnabled_;
        if (!legacy) return;
        legacy->setOpacity(opacity);if(!legacy)return;
        legacy->setEnabled(enabled);if(!legacy)return;
        if (!propertyObject(legacy,"controller")) {
            QObject* current=storage?propertyObject(storage,"photos"):controller.data();
            legacy->setProperty("controller",QVariant::fromValue(current));
        }
    }
    void fallback() {
        const QPointer<PhotoSession> self(this);model_->hide();if(!self)return;restoreLegacy();
    }
    void pickFiles() {
        if (!host_ || !bridge_ || !wanted()) return;
        const QPointer<PhotoSession> self(this);
        ii::gui::FileDialogOptions options{.mode=ii::gui::FileDialogMode::open,.title=strings_->get("photo.add"),.multiple=true};
        auto admitted=picker_.open([host=host_](auto request,auto completion){
            return host->choose_file(std::move(request),std::move(completion));
        },std::move(options),[self](ii::core::Result<ii::gui::FileDialogSelection> selected){
            if(!self || !self->bridge_ || !self->wanted())return;
            if(!selected){self->pickerFailed();return;}
            if(!selected->canceled)self->bridge_->addFiles(selected->paths);
        });
        if(self && !admitted)self->pickerFailed();
    }
    void pickerFailed(){if(model_&&strings_)model_->status.set(strings_->get("photo.picker_failed"));}
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> storage_,legacy_,overlay_;
    QPointer<iiPhotoLibrary::PhotoController> controller_;
    QPointer<NativeModalPresence> modal_;
    PhotoSurfaceFactory factory_;
    std::function<void()> invalidated_;
    std::function<void()> bindingDetached_;
    std::shared_ptr<Model> model_;
    std::shared_ptr<lvui::native::AppStrings> strings_;
    std::unique_ptr<PhotoPreviewLoader> loader_;
    std::shared_ptr<PhotoPreviewResolver> resolver_;
    std::shared_ptr<lvui::native::ImageServices<ii::gui::RasterImage>> images_;
    std::unique_ptr<photos::PhotoPresentationBridge> bridge_;
    std::unique_ptr<NativeOverlayCoordinator> coordinator_;
    std::unique_ptr<PhotoAreaTracker> tracker_;
    ii::core::ObservableSubscription modalSubscription_;
    std::weak_ptr<PhotoSurface> surface_;
    PhotoPicker picker_;
    ii::gui::Host* host_=nullptr;
    PhotoArea area_;
    qreal previousOpacity_=1;
    bool previousEnabled_=true,suppressed_=false,stopping_=false;
};

class NativePhotoPresenter final : public QObject {
    Q_OBJECT
public:
    NativePhotoPresenter(QQuickWindow* window,QObject* loader,QQuickItem* overlay,NativeModalPresence* modal,
                         PhotoSurfaceFactory factory,QObject* parent)
        :QObject(parent),window_(window),loader_(loader),overlay_(overlay),modal_(modal),factory_(std::move(factory)) {
        if (!watchProperty(loader,"item",loaderConnection_)) throw std::invalid_argument("Storage loader does not notify item changes");
        const QPointer<NativePhotoPresenter> self(this);
        connect(loader,&QObject::destroyed,this,[self]{if(self)self->scheduleScan();});
        connect(window,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
    }
    ~NativePhotoPresenter() override {shutdown({});}
    void begin(){scheduleScan();}
    void shutdown(std::function<void()> done) {
        closing_=true;QObject::disconnect(loaderConnection_);QObject::disconnect(storageConnection_);QObject::disconnect(legacyConnection_);
        if (session_) session_->shutdown(std::move(done));
        else if (retiring_) retiring_->shutdown(std::move(done));
        else if(done)done();
    }
private slots:
    void scheduleScan(){
        if (closing_ || scheduled_) return;scheduled_=true;
        QTimer::singleShot(0,this,[this]{scheduled_=false;scan();});
    }
private:
    bool watchProperty(QObject* source,const char* name,QMetaObject::Connection& connection){
        if(!source)return false;
        const auto propertyIndex=source->metaObject()->indexOfProperty(name);
        const auto slotIndex=metaObject()->indexOfSlot("scheduleScan()");
        if(propertyIndex<0 || slotIndex<0)return false;
        const auto property=source->metaObject()->property(propertyIndex);
        if(!property.hasNotifySignal())return false;
        connection=QObject::connect(source,property.notifySignal(),this,metaObject()->method(slotIndex));
        return bool(connection);
    }
    void scan(){
        if (closing_ || retiring_) return;
        auto* storage=qobject_cast<QQuickItem*>(propertyObject(loader_,"item"));
        if (storage_!=storage) {
            QObject::disconnect(storageConnection_);storage_=storage;
            if(storage_ && !watchProperty(storage_,"photos",storageConnection_)) storage_=nullptr;
        }
        auto* legacy=storage_?storage_->findChild<QQuickItem*>(QStringLiteral("photosView")):nullptr;
        if (watchedLegacy_!=legacy) {
            QObject::disconnect(legacyConnection_);watchedLegacy_=legacy;
            if (watchedLegacy_ && !watchProperty(watchedLegacy_,"controller",legacyConnection_)) watchedLegacy_=nullptr;
        }
        if (!watchedLegacy_) legacy=nullptr;
        auto* controller=qobject_cast<iiPhotoLibrary::PhotoController*>(propertyObject(storage_,"photos"));
        if (session_ && session_->matches(storage_,legacy,controller)) {
            if(bindingDetached_ && !session_->suppressed() && propertyObject(legacy,"controller")!=controller)
                legacy->setProperty("controller",QVariant::fromValue(static_cast<QObject*>(controller)));
            return;
        }
        if (session_) {
            const auto old=session_;session_=nullptr;retiring_=old;
            const QPointer<NativePhotoPresenter> self(this);
            old->shutdown([self,old]{
                if(self && self->retiring_==old){self->retiring_=nullptr;if(!self->closing_)self->scheduleScan();}
                if(old)old->deleteLater();
            });
            return;
        }
        if (bindingDetached_ && legacy && propertyObject(legacy,"controller")!=controller) {
            const QPointer<NativePhotoPresenter> self(this);const QPointer<QQuickItem> item(legacy);
            legacy->setProperty("controller",QVariant::fromValue(static_cast<QObject*>(controller)));
            if(!self || !item)return;
        }
        if (!window_ || !overlay_ || !modal_ || !storage_ || !legacy || !controller ||
            legacy->metaObject()->indexOfProperty("controller")<0) return;
        const QPointer<NativePhotoPresenter> self(this);
        try {session_=new PhotoSession(window_,storage_,legacy,controller,overlay_,modal_,factory_,
            [self]{if(self)self->scheduleScan();},[self]{if(self)self->bindingDetached_=true;},this);}
        catch (const std::exception& error) { qWarning() << "Cannot create the native photo session:" << error.what(); }
        catch (...) { qWarning() << "Cannot create the native photo session: unknown error"; }
    }
    QPointer<QQuickWindow> window_;
    QPointer<QObject> loader_;
    QPointer<QQuickItem> overlay_,storage_,watchedLegacy_;
    QPointer<NativeModalPresence> modal_;
    PhotoSurfaceFactory factory_;
    QPointer<PhotoSession> session_,retiring_;
    QMetaObject::Connection loaderConnection_,storageConnection_,legacyConnection_;
    bool scheduled_=false,closing_=false,bindingDetached_=false;
};
QQuickItem* popupOverlay(QQuickWindow* window){
    if (!window || !window->contentItem()) return nullptr;
    for (auto* child:window->contentItem()->childItems()) if (child->inherits("QQuickOverlay")) return child;
    return nullptr;
}
}
bool installNativePhotoPresenter(QObject* root,QObject* lifetime,OverlayShutdown* shutdown,QQuickItem* overlay,PhotoSurfaceFactory factory) {
    auto* window=qobject_cast<QQuickWindow*>(root);
    if (!window || !lifetime || !shutdown || window->thread()!=QThread::currentThread() ||
        lifetime->thread()!=QThread::currentThread() || shutdown->thread()!=QThread::currentThread() || shutdown->isClosing()) return false;
    auto* loader=window->findChild<QObject*>(QStringLiteral("storageLoader"));
    if (!overlay) overlay=popupOverlay(window);
    if (!loader || !overlay || loader->thread()!=QThread::currentThread() || overlay->thread()!=QThread::currentThread()) return false;
    auto* modal=NativeModalPresence::forWindow(window);
    if (!modal) return false;
    try {
        if(!factory) factory=[](QWindow* host,PhotoArea area,PhotoPredicate wanted,PhotoMount mount,
                                PhotoPredicate ready,std::function<void()> retired)->std::shared_ptr<PhotoSurface>{
            return std::make_shared<ExternalPhotoSurface>(host,area,std::move(wanted),std::move(mount),std::move(ready),std::move(retired));
        };
        auto presenter=std::make_unique<NativePhotoPresenter>(window,loader,overlay,modal,std::move(factory),lifetime);
        const QPointer<NativePhotoPresenter> guard(presenter.get());
        shutdown->add([guard](std::function<void()> done){if(guard)guard->shutdown(std::move(done));else if(done)done();});
        presenter->begin();
        if (!guard) {(void)presenter.release();return false;}
        presenter.release();return true;
    } catch (...) {return false;}
}
}
#include "NativePhotoPresenter.moc"
