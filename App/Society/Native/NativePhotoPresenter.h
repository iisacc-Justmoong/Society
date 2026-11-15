#pragma once
#include "NativeOverlayCoordinator.h"
#include "PhotoAreaTracker.h"
#include <functional>
#include <memory>
class QObject;
class QQuickItem;
class QWindow;
namespace ii::gui { class Host; }
namespace society::native {
class OverlayShutdown;
class PhotoSurface : public NativeOverlaySurface {
public:
    [[nodiscard]] virtual bool update(const PhotoArea&) noexcept=0;
};
using PhotoPredicate=std::function<bool()>;
using PhotoMount=std::function<bool(ii::gui::Host&)>;
using PhotoSurfaceFactory=std::function<std::shared_ptr<PhotoSurface>(
    QWindow*,PhotoArea,PhotoPredicate,PhotoMount,PhotoPredicate,std::function<void()>)>;
// The optional overlay parameter is for a host that already knows its popup
// layer. Otherwise the transitional Qt host discovers it from the window.
[[nodiscard]] bool installNativePhotoPresenter(QObject* root,QObject* lifetime,OverlayShutdown*,
                                               QQuickItem* overlay=nullptr,PhotoSurfaceFactory factory={});
}
