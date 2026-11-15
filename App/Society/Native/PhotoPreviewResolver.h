#pragma once

#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace society::native {

// Resolves previews only when a mounted view asks for them. The host owns file
// access and asynchronous decoding; it delivers completions on the owner thread.
class PhotoPreviewResolver final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
public:
    using Image = std::shared_ptr<const ii::gui::RasterImage>;
    using Completion = std::function<void(Image)>;
    using Loader = std::function<void(std::string, std::uint32_t, std::uint32_t, Completion)>;
    struct Limits final { std::size_t entries=0, bytes=0; };

    PhotoPreviewResolver(Limits, Loader);
    ~PhotoPreviewResolver() override;
    PhotoPreviewResolver(const PhotoPreviewResolver&)=delete;
    PhotoPreviewResolver& operator=(const PhotoPreviewResolver&)=delete;

    [[nodiscard]] Image image(std::string_view source, std::uint32_t width, std::uint32_t height) const override;
    void invalidate(std::string_view source);
private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace society::native
