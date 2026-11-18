#include "Society/Native/CoreDeadlineScheduler.h"
#include "AccountDialog.generated.h"
#include <iiAccountManager>
#include <iiGUI>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

using ii::core::Result;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void good(Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
template<class T>T take(Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
struct Device final : ii::gui::PaintDevice {
    std::vector<std::string> text;
    ii::gui::PaintCapabilities capabilities()const noexcept override{return {.api=ii::gui::GraphicsApi::vulkan,.hardware=true,.native_api=true,.backdrop=true,.shadow=true,.content_transform=true,.scene_sources=true};}
    Result<void> begin_frame(const ii::gui::PaintFrameInfo&)override{return {};}
    Result<void> begin_scene(const ii::gui::PaintSceneInfo&)override{return {};}
    Result<void> end_scene()override{return {};}
    Result<void> begin_layer(const ii::gui::PaintLayerInfo&)override{return {};}
    Result<void> draw_panels(std::span<const ii::gui::Panel>)override{return {};}
    Result<void> draw_text(std::span<const ii::gui::TextRun> runs)override{for(const auto& run:runs)text.push_back(run.text);return {};}
    Result<void> draw_images(std::span<const ii::gui::ImageRun>)override{return {};}
    Result<void> end_layer()override{return {};}
    Result<void> present()override{return {};}
    void abort_frame()noexcept override{}
};
}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QObject owner;QNetworkAccessManager offline;
    iisacc::accounts::AccountManager manager(QUrl("https://invalid.invalid"),&offline);
    using namespace iisacc::accounts::presentation;
    auto model=std::make_shared<AccountViewModel>(society::native::coreDeadlineScheduler(&owner));
    AccountPresentationBridge bridge(&manager,model,[](std::string_view){});
    auto strings=std::make_shared<lvui::native::AppStrings>();
    good(strings->replace_catalog("en",{{"account.sign_in","Sign in"},{"account.create","Create an account"},{"account.close","Close"}}));
    const auto factories=lvui::native::BackendFactories{}.with_app_strings(strings).with<AccountBackend>([model]{return AccountBackend(model);});
    manager.showLogin();QCoreApplication::processEvents();check(model->open.get(),"The account service did not open the model");
    using View=lvui_generated::AccountDialog;std::shared_ptr<View::Scope> scope;ii::runtime::EventLoop loop;ii::runtime::FrameLoop frames(loop);
    auto device=std::make_shared<Device>();
    auto view=take(ii::gui::PaintView::create(device,[&](ii::gui::WidgetBuilder& b){return View::build(b,{}, {},&scope,factories);},
        {},ii::gui::PaintViewServices{.events=&loop,.frames=&frames}));
    const auto first=ii::runtime::Clock::now();(void)frames.step(first);(void)frames.step(first+std::chrono::milliseconds(250));
    good(view->paint({640,600}));
    auto sheet=take(view->find("accountDialog"));
    check(std::ranges::find(device->text,"Sign in")!=device->text.end(),"The compiled view lost localized account state");
    auto close=take(sheet.popup_content("close"));good(close.activate());QCoreApplication::processEvents();
    check(manager.activeView()==iisacc::accounts::AccountManager::View::Closed&&!model->open.get(),"The compiled close did not reach the account service");
    good(view->close());check(!close.snapshot(),"The account view retained widgets after close");
}
