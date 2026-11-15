#include "Society/Native/CoreDeadlineScheduler.h"
#include "Society/Native/NativeAccountPresenter.h"
#include "Society/Native/NativeOverlayCoordinator.h"
#include "Society/Native/OverlayShutdown.h"
#include <iiAccountManager/AccountManager.h>
#include <iiAccountManager/Quick/AccountViews.h>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QWindow>
#include <chrono>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
namespace {
struct TestSurface final : society::native::NativeOverlaySurface {
    bool accepted=true;int starts=0,closes=0;std::function<void(bool)> closed;
    bool start(std::function<void(bool)> callback)override{++starts;closed=std::move(callback);return accepted;}
    void requestClose()noexcept override{++closes;}
    void fail(){auto callback=std::move(closed);if(callback)callback(true);}
};
void drain(){for(int i=0;i<4;++i)QCoreApplication::processEvents();}
}
int main(int argc,char** argv){
    using namespace std::chrono_literals;
    QGuiApplication app(argc,argv);QObject lifetime;
    society::native::OverlayShutdown shutdown;
    QNetworkAccessManager offline;
    iisacc::accounts::AccountManager manager(QUrl("https://invalid.invalid"),&offline);
    iisacc::accounts::AccountViews legacy;legacy.setManager(&manager);
    QWindow root;QObject invalid;
    using society::native::installNativeAccountPresenter;
    check(!installNativeAccountPresenter(&invalid,&manager,&legacy,&lifetime,&shutdown),"A non-window root was admitted");
    check(legacy.manager()==&manager,"Failed admission detached the legacy view");
    manager.showLogin();
    check(installNativeAccountPresenter(&root,&manager,&legacy,&lifetime,&shutdown),"Valid account host was rejected");
    check(legacy.manager()==&manager,"An already-open legacy dialog was detached");
    manager.closeView();QCoreApplication::processEvents();
    check(legacy.manager()==nullptr,"The closed legacy view did not hand off to native");
    manager.showSignUp();QCoreApplication::processEvents();
    check(!root.isVisible(),"Account presentation showed a native window in a headless test");
    QEvent language(QEvent::LanguageChange);QCoreApplication::sendEvent(&root,&language);

    iisacc::accounts::AccountManager second(QUrl("https://invalid.invalid"),&offline);
    iisacc::accounts::AccountViews reluctant;reluctant.setManager(&second);QWindow secondRoot;
    auto secondLifetime=std::make_unique<QObject>();bool reattached=false;
    QObject::connect(&reluctant,&iisacc::accounts::AccountViews::managerChanged,&reluctant,[&]{
        if(!reluctant.manager()&&!reattached){reattached=true;reluctant.setManager(&second);}
    });
    check(installNativeAccountPresenter(&secondRoot,&second,&reluctant,secondLifetime.get(),&shutdown),"Reentrant host was rejected");
    check(reluctant.manager()==&second,"A reentrant legacy reattachment was ignored");
    second.showLogin();second.closeView();QCoreApplication::processEvents();
    check(!reluctant.manager(),"The native handoff did not recover after reattachment");
    secondLifetime.reset();check(reluctant.manager()==&second,"Presenter destruction did not restore the legacy view");

    for(const int failureMode : {0,1,2}){
        iisacc::accounts::AccountManager account(QUrl("https://invalid.invalid"),&offline);
        iisacc::accounts::AccountViews fallback;fallback.setManager(&account);
        QWindow window;QObject owner;
        auto surface=std::make_shared<TestSurface>();surface->accepted=failureMode==1;
        int factoryCalls=0;
        society::native::AccountSurfaceFactory factory=[&](QWindow*,auto,auto,auto)->std::shared_ptr<society::native::NativeOverlaySurface>{
            ++factoryCalls;if(failureMode==2)throw std::runtime_error("surface factory failed");return surface;
        };
        check(installNativeAccountPresenter(&window,&account,&fallback,&owner,&shutdown,factory),
              "Account presenter rejected an injected surface");
        check(!fallback.manager(),"Native presenter did not take over the closed account view");
        window.setVisible(true);account.showLogin();drain();
        check(factoryCalls==1&&(failureMode==2||surface->starts==1),"The requested account surface was not started");
        if(failureMode==1){
            check(!fallback.manager(),"Legacy view returned before surface retirement");
            surface->fail();drain();
        }
        check(fallback.manager()==&account,"Surface failure did not restore the account view");
        check(account.activeView()==iisacc::accounts::AccountManager::View::Login,
              "Surface failure discarded the open account request");
        drain();check(factoryCalls==1,"Surface failure caused an unsolicited retry");
        account.closeView();drain();
        check(fallback.manager()==&account,"A closed failed presentation detached its fallback");
        account.showLogin();drain();
        check(fallback.manager()==&account&&factoryCalls==1,
              "A later login lost its fallback or retried a failed surface");
        window.setVisible(false);
    }

    {
        iisacc::accounts::AccountManager account(QUrl("https://invalid.invalid"),&offline);
        iisacc::accounts::AccountViews fallback;fallback.setManager(&account);
        QWindow window;QObject owner;
        check(installNativeAccountPresenter(&window,&account,&fallback,&owner,&shutdown),
              "The external handoff fixture was rejected");
        check(!fallback.manager(),"The external handoff fixture did not begin detached");
        fallback.setManager(&account);drain();
        account.showLogin();account.closeView();drain();
        check(fallback.manager()==&account,"An externally restored fallback was detached again");
    }

    QObject schedulerOwner;auto schedule=society::native::coreDeadlineScheduler(&schedulerOwner);
    bool cancelledFired=false,deadlineFired=false,early=false;
    auto cancelled=schedule(10ms,[&]{cancelledFired=true;});check(bool(cancelled),"Cancellation task was not scheduled");(*cancelled)();
    QEventLoop loop;const auto started=std::chrono::steady_clock::now();
    auto deadline=schedule(20ms,[&]{deadlineFired=true;early=std::chrono::steady_clock::now()-started<20ms;loop.quit();});
    check(bool(deadline),"Deadline task was not scheduled");
    QTimer::singleShot(1000,&loop,&QEventLoop::quit);loop.exec();
    check(deadlineFired&&!early&&!cancelledFired,"Core deadline or cancellation was not respected");
    check(!schedule(-1ms,[]{}),"A negative deadline was accepted");
    auto shortLived=std::make_unique<QObject>();auto orphanSchedule=society::native::coreDeadlineScheduler(shortLived.get());
    bool orphanFired=false;auto orphan=orphanSchedule(10ms,[&]{orphanFired=true;});check(bool(orphan),"Owner task was not scheduled");
    shortLived.reset();QEventLoop drain;QTimer::singleShot(30,&drain,&QEventLoop::quit);drain.exec();
    check(!orphanFired&&!orphanSchedule(0ms,[]{}),"A destroyed owner retained deadline work");
    QEvent quit(QEvent::Quit);QCoreApplication::sendEvent(&app,&quit);
    check(shutdown.isClosing(),"Account surface did not join app shutdown");
    check(!installNativeAccountPresenter(&root,&manager,&legacy,&lifetime,&shutdown),"A closing app admitted an account view");
    QCoreApplication::processEvents();
}
