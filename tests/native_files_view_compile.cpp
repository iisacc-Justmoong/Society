#include "NativeFiles.generated.h"
#include "NativeFilesNotification.h"
#include <type_traits>
static_assert(std::is_default_constructible_v<lvui_generated::NativeFiles>);
static_assert(std::is_default_constructible_v<NativeFilesNavigation>);
#if SOCIETY_IIGUI_TEST_WIDGETS
#include <iiGUI/WidgetState.h>
#include <iostream>
#include <stdexcept>
template<class T> T take(ii::core::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
void good(ii::core::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
ii::gui::Widget widget(const std::shared_ptr<ii::gui::detail::WidgetState> &tree,std::string_view suffix) {
    for (const auto &node:take(tree->snapshots()))
        if (node.id.ends_with(suffix)) return take(tree->find(node.id));
    throw std::runtime_error("Missing notification popup");
}
int main() try {
    using View=lvui_generated::NativeFiles;
    ii::runtime::EventLoop events;
    ii::runtime::FrameLoop frames(events);
    ii::gui::detail::WidgetFrameHooks hooks;
    hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &events;};
    hooks.frames=[&]() -> ii::core::Result<ii::runtime::FrameLoop *> {return &frames;};
    auto tree=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
    auto host=take(tree->content_host({},"files-test-host",{1100,720}));
    std::shared_ptr<View::Scope> scope;
    good(host.set_content([&](auto &builder){return View::build(builder,{}, {},&scope);}));
    (void)take(tree->drawing_snapshot({1100,720}));
    auto popup=widget(tree,"/society-files-error-notification");
    scope->backend_0.backendError("Connection failed");
    society::native::presentFilesNotification(popup,1100,scope->backend_0);
    (void)take(tree->drawing_snapshot({1100,720}));
    auto bounds=take(popup.snapshot()).bounds;
    if (bounds.x<700 || bounds.y>40 || take(take(popup.popup_content("files-notification-message")).snapshot()).text!="Connection failed")
        throw std::runtime_error("The Files notification was not shown at the upper right");
    good(take(popup.popup_content("dismiss-files-error-notification")).activate());
    society::native::presentFilesNotification(popup,1100,scope->backend_0);
    if (take(popup.snapshot()).opened || !scope->backend_0.hasErrors.get())
        throw std::runtime_error("Dismissing a notification lost the error history");
    tree->close();return 0;
} catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
#else
int main() { return 0; }
#endif
