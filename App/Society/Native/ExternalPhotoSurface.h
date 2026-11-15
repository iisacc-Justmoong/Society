#pragma once
#include "NativePhotoPresenter.h"
#include <QPointer>
#include <QWindow>
#include <functional>
#include <memory>

namespace ii::gui { class Host; }
namespace society::native {
class PhotoNativeRegion;
class ExternalPhotoSurface final : public PhotoSurface,
                                   public std::enable_shared_from_this<ExternalPhotoSurface> {
public:
    using Predicate=PhotoPredicate;
    using Mount=PhotoMount;
    ExternalPhotoSurface(QWindow*,PhotoArea,Predicate,Mount,Predicate ready,std::function<void()> retired);
    ~ExternalPhotoSurface() override;
    bool start(std::function<void(bool)> closed) override;
    void requestClose() noexcept override;
    // Applies only a changed position/clip or parent size; a hidden area is
    // immediately made non-interactive while deferred close completes.
    [[nodiscard]] bool update(const PhotoArea&) noexcept override;
private:
    void mounted();
    void retired();
    void fail() noexcept;
    QPointer<QWindow> window_;
    PhotoArea area_;
    Predicate wanted_;
    Mount mount_;
    Predicate ready_;
    std::function<void()> retired_;
    std::function<void(bool)> closed_;
    std::unique_ptr<PhotoNativeRegion> region_;
    std::unique_ptr<ii::gui::Host> host_;
    bool closing_=false,failed_=false;
};
} // namespace society::native
