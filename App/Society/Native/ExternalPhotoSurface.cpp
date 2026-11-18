#include "ExternalPhotoSurface.h"
#include "PhotoNativeRegion.h"
#include <iiGUI>
#include <QCoreApplication>
#include <QTimer>
#include <utility>

namespace society::native {
ExternalPhotoSurface::ExternalPhotoSurface(QWindow* window,PhotoArea area,Predicate wanted,Mount mount,
                                           Predicate ready,std::function<void()> retired)
    :window_(window),area_(area),wanted_(std::move(wanted)),mount_(std::move(mount)),
     ready_(std::move(ready)),retired_(std::move(retired)) {}
ExternalPhotoSurface::~ExternalPhotoSurface()=default;
bool ExternalPhotoSurface::start(std::function<void(bool)> closed) {
    if (!window_ || !window_->isVisible() || area_.mode!=PhotoArea::Mode::visible ||
        !wanted_ || !mount_ || !ready_ || !wanted_()) return false;
    region_=PhotoNativeRegion::create(reinterpret_cast<void*>(window_->winId()),area_);
    if (!region_) return false;
    closed_=std::move(closed);host_=std::make_unique<ii::gui::Host>();
    const auto self=shared_from_this();
    auto attached=host_->attach_external(region_->nativeParent(),{
        .opened=[self]{self->mounted();},.closed=[self]{self->retired();}
    });
    if (!attached) {host_.reset();region_.reset();closed_={};return false;}
    return true;
}
void ExternalPhotoSurface::requestClose() noexcept {
    if (region_) (void)region_->setVisible(false);
    if (!host_ || closing_) return;
    closing_=true;const auto weak=weak_from_this();
    auto close=[weak]{if(auto self=weak.lock();self&&self->host_)(void)self->host_->request_close();};
    if (!host_->executor().post(close) && qApp) QTimer::singleShot(0,qApp,std::move(close));
}
bool ExternalPhotoSurface::update(const PhotoArea& next) noexcept {
    const auto previous=area_;area_=next;
    try {
        if (!region_) return true;
        if (next.mode!=PhotoArea::Mode::visible) { (void)region_->setVisible(false);return true; }
        if (!region_->update(next)) {fail();return false;}
        if (host_ && (previous.bounds.size()!=next.bounds.size() || previous.deviceScale!=next.deviceScale) &&
            !host_->external_parent_resized()) {
            fail();return false;
        }
        return true;
    } catch (...) {fail();return false;}
}
void ExternalPhotoSurface::mounted() {
    try {
        if (!host_ || closing_ || !window_ || !window_->isVisible() || area_.mode!=PhotoArea::Mode::visible || !wanted_()) {
            requestClose();return;
        }
        const bool ready=ready_();
        if (closing_ || !wanted_()) {requestClose();return;}
        if (!ready || !mount_(*host_)) {fail();return;}
        if (closing_ || !wanted_()) {requestClose();return;}
        if (region_ && region_->setVisible(true)) {
            // The external host was attached while its clipped parent was
            // hidden. Refresh its snapshot after revealing the parent.
            if (!host_ || !host_->external_parent_resized()) {fail();return;}
            if (closing_ || !wanted_()) requestClose();
            return;
        }
    } catch (...) {}
    fail();
}
void ExternalPhotoSurface::fail() noexcept {failed_=true;requestClose();}
void ExternalPhotoSurface::retired() {
    auto completed=std::move(closed_);
    if (retired_) try {retired_();} catch (...) {}
    host_.reset();region_.reset();
    if (completed) completed(failed_);
}
} // namespace society::native
