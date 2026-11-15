#pragma once
#include "PhotoAreaTracker.h"
#include <memory>

namespace society::native {
// Owns a clipped native parent for an iiGUI surface. The caller must close the
// attached host before destroying this object, and use it on the main thread.
class PhotoNativeRegion final {
public:
    static std::unique_ptr<PhotoNativeRegion> create(void* nativeParent,const PhotoArea&) noexcept;
    ~PhotoNativeRegion();
    PhotoNativeRegion(const PhotoNativeRegion&)=delete;
    PhotoNativeRegion& operator=(const PhotoNativeRegion&)=delete;
    [[nodiscard]] void* nativeParent() const noexcept;
    [[nodiscard]] bool update(const PhotoArea&) noexcept;
    [[nodiscard]] bool setVisible(bool) noexcept;
private:
    struct Impl;
    explicit PhotoNativeRegion(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace society::native
