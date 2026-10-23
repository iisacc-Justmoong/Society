#include "CanvasFilePreview.h"
#include <Render/DocumentPreview.h>
#include <iiFileProviderPresentation.h>
#include <QBuffer>
#include <QImage>

void registerSocietyCanvasFilePreview()
{
    using namespace iiFileProvider::native;
    Presentation::setRenderer(".iisc", [](const auto &path, auto &properties) -> std::optional<FilePreview> {
        iiSharedCanvas::SerializationLimits limits;
        limits.maximumContainerBytes = Presentation::maximumSourceBytes;
        limits.maximumCanvasPixels = 16ull * 1024 * 1024;
        limits.maximumTotalRasterPixels = 16ull * 1024 * 1024;
        const auto utf8 = path.u8string();
        const auto preview = iiSharedCanvas::readIiscPreview(
            std::string(utf8.begin(), utf8.end()), {{512, 512}}, limits);
        if (!preview.ok()) return {};
        const auto &pixels = preview.pixels;
        const QImage image(reinterpret_cast<const uchar *>(pixels.pixels.data()),
            pixels.width, pixels.height, pixels.width * 4, QImage::Format_ARGB32);
        QByteArray png; QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
        properties["width"] = std::to_string(preview.region.extent.width);
        properties["height"] = std::to_string(preview.region.extent.height);
        return FilePreview{"image/png", "image", png.toStdString(),
            static_cast<std::uint32_t>(pixels.width), static_cast<std::uint32_t>(pixels.height)};
    });
}
