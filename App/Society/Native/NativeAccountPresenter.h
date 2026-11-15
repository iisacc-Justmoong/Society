#pragma once
#include <functional>
#include <memory>
class QObject;
class QWindow;
namespace ii::gui { class Host; }
namespace iisacc::accounts { class AccountManager; class AccountViews; }
namespace society::native {
class OverlayShutdown;
class NativeOverlaySurface;
using AccountSurfaceFactory=std::function<std::shared_ptr<NativeOverlaySurface>(
    QWindow*,std::function<bool()>,std::function<bool(ii::gui::Host&)>,std::function<void()>)>;
// The caller owns the Qt account service and legacy host. Native admission
// never changes the authenticated session or stores credentials.
[[nodiscard]] bool installNativeAccountPresenter(QObject* root,iisacc::accounts::AccountManager*,
    iisacc::accounts::AccountViews*,QObject* lifetime,OverlayShutdown*);
// A host may supply its own surface implementation while keeping the same
// view, model, lifetime and fallback behavior.
[[nodiscard]] bool installNativeAccountPresenter(QObject* root,iisacc::accounts::AccountManager*,
    iisacc::accounts::AccountViews*,QObject* lifetime,OverlayShutdown*,AccountSurfaceFactory);
}
