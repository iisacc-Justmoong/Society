#include "Society/Mcp/NativeQuestionPresenter.h"
#include "Society/Native/OverlayShutdown.h"
#include "Society/Native/ExternalOverlaySurface.h"
#include "Society/Native/NativeModalPresence.h"
#include <iiMultimodalAgent>
#include <QEvent>
#include <QGuiApplication>
#include <QWindow>
#include <memory>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);society::native::OverlayShutdown shutdown;
    iiMultimodalAgent::agent::QuestionInbox inbox;
    QObject lifetime,invalidRoot;QWindow root;
    auto* modal=society::native::NativeModalPresence::forWindow(&root);
    auto rejected=std::make_shared<society::native::ExternalOverlaySurface>(&root,[]{return true;},
        [](auto&){return true;},[]{});
    check(modal && !rejected->start([](bool){}) && modal->count().get()==0,
          "A hidden surface acquired a modal lease");
    using society::questions::installNativeQuestionPresenter;
    check(!installNativeQuestionPresenter(&invalidRoot,&inbox,&lifetime,&shutdown),"A non-window root was admitted");
    check(!installNativeQuestionPresenter(&root,nullptr,&lifetime,&shutdown),"A missing inbox was admitted");
    check(!installNativeQuestionPresenter(&root,&inbox,nullptr,&shutdown),"A missing lifetime was admitted");
    check(!installNativeQuestionPresenter(&root,&inbox,&lifetime,nullptr),"A missing shutdown owner was admitted");
    check(installNativeQuestionPresenter(&root,&inbox,&lifetime,&shutdown),"A hidden window was not admitted");
    QEvent language(QEvent::LanguageChange);QCoreApplication::sendEvent(&root,&language);
    QCoreApplication::processEvents();check(!root.isVisible(),"The presenter showed an application window");
    QEvent quit(QEvent::Quit);QCoreApplication::sendEvent(&app,&quit);
    check(shutdown.isClosing(),"Application shutdown was not coordinated");
    check(!installNativeQuestionPresenter(&root,&inbox,&lifetime,&shutdown),"A closing application admitted a presenter");
    QCoreApplication::processEvents();
}
