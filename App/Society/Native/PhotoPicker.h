#pragma once
#include <iiGUI>
#include <functional>
#include <memory>

namespace society::native {
// A single owner-thread picker request. Cancellation retires its completion;
// the surface remains responsible for dismissing any native picker it owns.
class PhotoPicker final {
public:
    using Transport=std::function<ii::core::Result<void>(ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion)>;
    PhotoPicker();
    ~PhotoPicker();
    PhotoPicker(const PhotoPicker&)=delete;
    PhotoPicker& operator=(const PhotoPicker&)=delete;
    [[nodiscard]] ii::core::Result<void> open(Transport,ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion);
    [[nodiscard]] ii::core::Result<void> cancel();
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
