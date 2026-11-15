#include "NativeAccountPresenter.h"
#include "CoreDeadlineScheduler.h"
#include "ExternalOverlaySurface.h"
#include "NativeOverlayCoordinator.h"
#include "OverlayShutdown.h"
#include "AccountDialog.generated.h"
#include <iiAccountManager/AccountManager.h>
#include <iiAccountManager/AccountPresentationBridge.h>
#include <iiAccountManager/Quick/AccountViews.h>
#include <iiGUI/Host.h>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <memory>
#include <stdexcept>
#include <utility>

namespace society::native {
namespace {
namespace accounts=iisacc::accounts;
namespace presentation=iisacc::accounts::presentation;
using View=lvui_generated::AccountDialog;
using Model=presentation::AccountViewModel;

bool loadCatalog(lvui::native::AppStrings& strings,const char* locale){
    QFile file(QStringLiteral(":/iiAccountManager/native-strings/")+QString::fromLatin1(locale)+QStringLiteral(".json"));
    if(!file.open(QIODevice::ReadOnly)||file.size()>static_cast<qint64>(lvui::native::AppStrings::maximum_catalog_bytes))return false;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())return false;
    const auto catalog=document.object();if(catalog.value("locale").toString()!=QString::fromLatin1(locale)||!catalog.value("strings").isObject())return false;
    lvui::native::AppStrings::Entries entries;
    const auto values=catalog.value("strings").toObject();
    for(auto it=values.begin();it!=values.end();++it){if(!it.value().isString())return false;entries.emplace(it.key().toStdString(),it.value().toString().toStdString());}
    return bool(strings.replace_catalog(locale,std::move(entries)));
}

class NativeAccountPresenter final : public QObject {
    Q_OBJECT
public:
    NativeAccountPresenter(QWindow* root,accounts::AccountManager* manager,accounts::AccountViews* legacy,
                           AccountSurfaceFactory factory,QObject* parent)
        :QObject(parent),root_(root),manager_(manager),legacy_(legacy),strings_(std::make_shared<lvui::native::AppStrings>()),
         model_(std::make_shared<Model>(coreDeadlineScheduler(this))){
        if(!loadCatalog(*strings_,"en")||!loadCatalog(*strings_,"ko"))throw std::runtime_error("Cannot load account view strings");
        translate();
        bridge_=std::make_unique<presentation::AccountPresentationBridge>(manager,model_,[](std::string_view address){
            (void)QDesktopServices::openUrl(QUrl(QString::fromUtf8(address.data(),static_cast<qsizetype>(address.size()))));
        });
        if(!factory)factory=[](QWindow* window,std::function<bool()> wanted,
                               std::function<bool(ii::gui::Host&)> mount,std::function<void()> unmount){
            return std::make_shared<ExternalOverlaySurface>(window,std::move(wanted),std::move(mount),std::move(unmount));
        };
        const QPointer<NativeAccountPresenter> self(this);
        coordinator_=std::make_unique<NativeOverlayCoordinator>(
            [self,window=QPointer<QWindow>(root),model=model_,strings=strings_,factory=std::move(factory)]()->std::shared_ptr<NativeOverlaySurface>{
                if(!self||!window)return {};
                auto scope=std::make_shared<std::shared_ptr<View::Scope>>();
                return factory(window.data(),
                    [self,window,model]{return self&&self->detached_&&window&&window->isVisible()&&model->open.get();},
                    [model,strings,scope](ii::gui::Host& host){
                        const auto factories=lvui::native::BackendFactories{}.with_app_strings(strings)
                            .with<presentation::AccountBackend>([model]{return presentation::AccountBackend(model);});
                        return bool(View::mount(host,{},scope.get(),factories));
                    },[scope]{scope->reset();});
            },
            [self](NativeOverlayCoordinator::Task task){if(self)QTimer::singleShot(0,self,[task=std::move(task)]{task();});},
            [self]{if(self)self->fallback();});
        open_=model_->open.observe([self](bool,std::uint64_t){if(self)self->refreshWanted();});
        connect(root,&QWindow::visibleChanged,this,[self](bool){if(self)self->refreshWanted();});
        connect(root,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
        connect(manager,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
        connect(legacy,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
        connect(manager,&accounts::AccountManager::activeViewChanged,this,[self]{if(self)self->detachIfClosed();});
        connect(legacy,&accounts::AccountViews::managerChanged,this,[self]{
            if(self&&self->detached_&&self->legacy_&&self->legacy_->manager()){
                self->handoffDisabled_=true;self->detached_=false;
                self->open_.reset();self->coordinator_->shutdown({});
            }
        });
        root->installEventFilter(this);if(qApp)qApp->installEventFilter(this);
    }
    ~NativeAccountPresenter()override{
        const bool restore=detached_&&!shuttingDown_;detached_=false;open_.reset();
        const QPointer<accounts::AccountViews> legacy=legacy_;const QPointer<accounts::AccountManager> manager=manager_;
        if(coordinator_)coordinator_->shutdown([restore,legacy,manager]{if(restore&&legacy&&manager&&!legacy->manager())legacy->setManager(manager);});
    }
    void begin(){const QPointer<NativeAccountPresenter> self(this);detachIfClosed();if(self)self->refreshWanted();}
    void shutdown(std::function<void()> completed){
        shuttingDown_=true;open_.reset();
        if(coordinator_)coordinator_->shutdown(std::move(completed));else if(completed)completed();
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event)override{
        if((watched==qApp||watched==root_)&&event->type()==QEvent::LanguageChange)translate();
        return QObject::eventFilter(watched,event);
    }
private:
    void translate(){
        if(!strings_)return;
        const auto locale=QLocale().bcp47Name().toStdString();
        (void)strings_->set_locale(locale.empty()?"en":locale);
    }
    void detachIfClosed(){
        if(shuttingDown_||handoffDisabled_||detached_||!legacy_||!manager_||legacy_->manager()!=manager_||
           manager_->activeView()!=accounts::AccountManager::View::Closed)return;
        const QPointer<NativeAccountPresenter> self(this);const auto legacy=legacy_;
        legacy->setManager(nullptr);
        if(!self||!legacy||legacy->manager())return;
        self->detached_=true;self->refreshWanted();
    }
    void refreshWanted(){if(coordinator_&&root_&&model_)coordinator_->setWanted(detached_&&root_->isVisible()&&model_->open.get());}
    void fallback(){
        handoffDisabled_=true;open_.reset();const bool restore=detached_;detached_=false;
        if(restore&&legacy_&&manager_&&!legacy_->manager())legacy_->setManager(manager_);
    }
    QPointer<QWindow> root_;
    QPointer<accounts::AccountManager> manager_;
    QPointer<accounts::AccountViews> legacy_;
    std::shared_ptr<lvui::native::AppStrings> strings_;
    std::shared_ptr<Model> model_;
    std::unique_ptr<presentation::AccountPresentationBridge> bridge_;
    std::unique_ptr<NativeOverlayCoordinator> coordinator_;
    ii::core::ObservableSubscription open_;
    bool detached_=false,shuttingDown_=false,handoffDisabled_=false;
};
}
bool installNativeAccountPresenter(QObject* root,accounts::AccountManager* manager,accounts::AccountViews* legacy,
                                   QObject* lifetime,OverlayShutdown* shutdown){
    return installNativeAccountPresenter(root,manager,legacy,lifetime,shutdown,{});
}
bool installNativeAccountPresenter(QObject* root,accounts::AccountManager* manager,accounts::AccountViews* legacy,
                                   QObject* lifetime,OverlayShutdown* shutdown,AccountSurfaceFactory factory){
    auto* window=qobject_cast<QWindow*>(root);
    if(!window||!manager||!legacy||!lifetime||!shutdown||legacy->manager()!=manager||
       window->thread()!=QThread::currentThread()||manager->thread()!=QThread::currentThread()||
       legacy->thread()!=QThread::currentThread()||lifetime->thread()!=QThread::currentThread()||
       shutdown->thread()!=QThread::currentThread()||shutdown->isClosing())return false;
    try{
        auto presenter=std::make_unique<NativeAccountPresenter>(window,manager,legacy,std::move(factory),lifetime);
        const QPointer<NativeAccountPresenter> guard(presenter.get());
        shutdown->add([guard](std::function<void()> done){if(guard)guard->shutdown(std::move(done));else if(done)done();});
        presenter->begin();
        if(!guard){(void)presenter.release();return false;}
        presenter.release();return true;
    }catch(...){return false;}
}
}
#include "NativeAccountPresenter.moc"
