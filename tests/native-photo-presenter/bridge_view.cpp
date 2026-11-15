#include "PhotosView.generated.h"
#include <iiGUI/PaintView.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <iiPhotoLibrary/PhotoPresentationBridge.h>
#include <QGuiApplication>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

using ii::core::Result;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void good(Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
template<class T>T take(Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
class FakeLibrary final : public iiPhotoLibrary::PhotoLibrary {
public:
    QString name()const override{return "Photo view fixture";}
    iiPhotoLibrary::Access access()const override{return iiPhotoLibrary::Access::Full;}
    void authorize(std::function<void()> done)override{done();}
    iiPhotoLibrary::Snapshot scan(QString*)override{return {};}
    bool preview(const QString&,const QString&,QString*)override{return false;}
    bool exportResource(const QString&,const QString&,const QString&,QString*)override{return false;}
    QString import(const QJsonObject&,const QStringList&,QString*)override{return {};}
    bool trash(const QString&,QString*)override{return false;}
};
struct Device final : ii::gui::PaintDevice {
    std::vector<std::string> text;
    std::vector<ii::gui::TextRun> runs;
    ii::gui::PaintCapabilities capabilities()const noexcept override{return {.api=ii::gui::GraphicsApi::vulkan,.hardware=true,.native_api=true,.backdrop=true,.shadow=true,.content_transform=true,.scene_sources=true};}
    Result<void> begin_frame(const ii::gui::PaintFrameInfo&)override{text.clear();runs.clear();return {};}
    Result<void> begin_scene(const ii::gui::PaintSceneInfo&)override{return {};}
    Result<void> end_scene()override{return {};}
    Result<void> begin_layer(const ii::gui::PaintLayerInfo&)override{return {};}
    Result<void> draw_panels(std::span<const ii::gui::Panel>)override{return {};}
    Result<void> draw_text(std::span<const ii::gui::TextRun> values)override{for(const auto& run:values){text.push_back(run.text);runs.push_back(run);}return {};}
    Result<void> draw_images(std::span<const ii::gui::ImageRun>)override{return {};}
    Result<void> end_layer()override{return {};}
    Result<void> present()override{return {};}
    void abort_frame()noexcept override{}
};
QVariant row(const char* id,const char* name){
    return QVariantMap{{"id",id},{"galleryKey",id},{"name",name},{"media","photo"},
                       {"created","2026-10-02T12:34:00Z"},
                       {"resources",QVariantList{QVariantMap{{"size",1024}},QVariantMap{{"size",512}}}}};
}
void events(){for(int i=0;i<4;++i)QCoreApplication::processEvents();}
}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);
    using namespace iiPhotoLibrary;
    using namespace iiPhotoLibrary::presentation;
    auto controller=std::make_unique<PhotoController>(PhotoController::Sender{},nullptr,std::make_shared<FakeLibrary>());
    auto* gallery=static_cast<PhotoGalleryModel*>(controller->galleryModel());
    gallery->replace({row("a","First photo"),row("b","Second photo")});
    auto model=std::make_shared<PhotoViewModel>();
    int pickerRequests=0;
    PhotoPresentationBridge bridge(controller.get(),model,[&]{++pickerRequests;model->status.set("Picker unavailable");});
    model->thumbnailSize.set(80);model->viewportWidth.set(320);model->viewportHeight.set(170);model->motionEnabled.set(false);model->show();
    model->active.set(true);model->busy.set(false);
    auto strings=std::make_shared<lvui::native::AppStrings>();
    good(strings->replace_catalog("en",{{"photo.title","Photos"},{"photo.add","Add photos"},{"photo.information","Information"},
        {"photo.field_type","Type: {value}"},{"photo.field_size","Size: {value}"},{"photo.field_created","Created: {value}"},
        {"photo.unit_bytes","{value} bytes"},{"photo.unit_kb","{value} KB"},{"photo.unit_mb","{value} MB"},{"photo.unit_gb","{value} GB"},
        {"photo.image","Photo"},{"photo.video","Video"}}));
    auto images=std::make_shared<lvui::native::ImageServices<ii::gui::RasterImage>>();
    const auto factories=lvui::native::BackendFactories{}.with_app_strings(strings)
        .with<PhotoBackend>([model]{return PhotoBackend(model);})
        .with<std::shared_ptr<lvui::native::ImageServices<ii::gui::RasterImage>>>([images]{return images;});
    using View=lvui_generated::PhotosView;
    ii::runtime::EventLoop loop;ii::gui::Widget host;std::shared_ptr<View::Scope> scope;
    auto device=std::make_shared<Device>();
    auto view=take(ii::gui::PaintView::create(device,[&](ii::gui::WidgetBuilder& builder)->Result<void>{
        host=take(builder.content_host({},"host",{900,720}));
        return host.set_content([&](ii::gui::WidgetBuilder& inner){return View::build(inner,{}, {},&scope,factories);});
    },{},ii::gui::PaintViewServices{.events=&loop}));
    good(view->paint({900,720}));
    const auto add=std::ranges::find_if(device->runs,[](const auto& run){return run.text=="Add photos";});
    check(add!=device->runs.end(),"The compiled photo view did not draw its add button");
    const auto addBounds=add->bounds;
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=addBounds.x+addBounds.width/2,.y=addBounds.y+addBounds.height/2,.buttons=1}));
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=addBounds.x+addBounds.width/2,.y=addBounds.y+addBounds.height/2}));
    check(pickerRequests==1,"The compiled add button did not invoke the host picker action");
    good(view->paint({900,720}));
    check(std::ranges::find(device->text,"Picker unavailable")!=device->text.end(),"A picker error was not reflected in the photo view");
    auto grid=take(host.content_item("photoGrid"));
    auto first=take(grid.keyed_content_item("a"));
    const auto bounds=take(first.snapshot()).bounds;
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=bounds.x+20,.y=bounds.y+20,.buttons=1}));
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=bounds.x+20,.y=bounds.y+20}));
    check(model->selectedId.get()=="a" && model->informationOpen.get(),"The compiled photo tile did not select the service item");
    good(view->paint({900,720}));
    check(std::ranges::find(device->text,"First photo")!=device->text.end(),"The compiled information sheet did not draw the photo service data");
    check(std::ranges::find(device->text,"Type: Photo")!=device->text.end(),"The photo type was not drawn");
    check(std::ranges::find(device->text,"Size: 1.5 KB")!=device->text.end(),"Resource sizes were not drawn together");
    const auto created="Created: "+PhotoBackend(model).formatTimestamp("2026-10-02T12:34:00Z");
    check(std::ranges::find(device->text,created)!=device->text.end(),"The service creation time was not drawn");
    gallery->replace({row("a","Renamed photo"),row("b","Second photo")});events();
    good(view->paint({900,720}));
    check(std::ranges::find(device->text,"Renamed photo")!=device->text.end(),"The compiled view did not reflect the photo service update");
    gallery->replace({row("b","Second photo")});events();good(view->paint({900,720}));
    check(model->selectedId.get().empty() && !model->informationOpen.get(),"Removing the service item retained a stale selection");
    good(view->close());check(!host.snapshot(),"Closing the compiled photo view retained its widgets");
}
