#include "Society/Native/QtPaintDevice.h"

#include <iiGUI>
#include <QGuiApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
using ii::gui::ImageRun;
using ii::gui::Rect;

bool preserves_device_pixels(double scale) {
    auto raster = ii::gui::device_rgba_image(2, 3, {
        255, 255,   0, 255,   0, 255, 255, 255,
        255,   0,   0, 255,   0, 255,   0, 128,
          0,   0, 255, 255, 255, 255, 255,   0,
    });
    if (!raster) return false;
    const auto logical = [scale](double pixel) { return static_cast<float>(pixel / scale); };
    const ImageRun run{
        .bounds = Rect{logical(2.67), logical(1.46), logical(2), logical(2)},
        .image = *raster,
        .opacity = 1,
        .source = Rect{0, 1.f / 3, 1, 2.f / 3},
    };
    QtPaintDevice painter;
    if (!painter.begin_frame({.viewport = {0, 0, logical(12), logical(12)}, .scale = scale,
                              .pixel_width = 12, .pixel_height = 12}) ||
        !painter.begin_layer({.transform = {.offset_x = logical(.48), .offset_y = logical(.36)}}) ||
        !painter.draw_images(std::span{&run, 1}) || !painter.end_layer() || !painter.present()) return false;
    const auto image = painter.image();
    const std::array expected{QColor(255, 0, 0), QColor(0, 255, 0, 128), QColor(0, 0, 255)};
    return image.devicePixelRatio() == scale && image.pixelColor(3, 2) == expected[0] &&
        image.pixelColor(4, 2) == expected[1] && image.pixelColor(3, 3) == expected[2] &&
        image.pixelColor(4, 3).alpha() == 0 && image.pixelColor(2, 2).alpha() == 0 &&
        image.pixelColor(3, 1).alpha() == 0 && image.pixelColor(5, 2).alpha() == 0;
}

bool preserves_native_label(double scale) {
    const auto logical = [scale](double pixel) { return static_cast<float>(pixel / scale); };
    ii::gui::TextRun run{
        .bounds = {logical(3.35), logical(3.85), 182, 36},
        .text = "안녕! Text conversation",
        .color = {1, 1, 1, .85f}, .font_size = 14.25f, .font_weight = 600,
        .elide_right = true, .letter_spacing = .45f,
    };
    const auto metrics = ii::gui::measure_wrapped_text_layout(run);
    if (!metrics) return false;
    auto local = run;
    local.bounds = {0, 0, run.bounds.width, std::min(run.bounds.height, static_cast<float>(metrics->height))};
    const auto native = ii::gui::render_text(local, scale);
    if (!native || native->rgba.empty()) return false;
    const auto tx = logical(.41), ty = logical(.41);
    const int left = int(std::lround((run.bounds.x + tx) * scale));
    const int top = int(std::lround((run.bounds.y + ty + (run.bounds.height - local.bounds.height) / 2) * scale));
    const int clipLeft = left + int(std::lround(12 * scale));
    const auto width = int(std::ceil(208 * scale)), height = int(std::ceil(60 * scale));
    QtPaintDevice painter;
    for (bool clipped : {false, true}) {
        if (clipped) run.clip = Rect{logical(clipLeft) - tx, 0, 190, 56};
        if (!painter.begin_frame({.viewport = {0, 0, 208, 60}, .scale = scale,
                                  .pixel_width = unsigned(width), .pixel_height = unsigned(height)}) ||
            !painter.begin_layer({.transform = {.offset_x = tx, .offset_y = ty}}) ||
            !painter.draw_text(std::span{&run, 1}) || !painter.end_layer() || !painter.present()) return false;
        const auto image = painter.image();
        bool coverage = false, cropsGlyph = false;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int alpha = 0;
                if (x >= left && y >= top && x - left < int(native->width) && y - top < int(native->height)) {
                    alpha = native->rgba[(std::size_t(y - top) * native->width + (x - left)) * 4 + 3];
                    if (clipped && x < clipLeft) { cropsGlyph |= alpha > 0; alpha = 0; }
                }
                coverage |= alpha > 0 && alpha < 255;
                if (qAlpha(image.pixel(x, y)) != alpha) return false;
            }
        }
        if (!coverage || (clipped && !cropsGlyph)) return false;
    }
    return true;
}
}

int main(int argc, char **argv) {
    const QGuiApplication app(argc, argv);
    for (double scale : {1., 1.25, 2., 3.}) {
        if (!preserves_device_pixels(scale)) {
            std::cerr << "The Society paint adapter altered device pixels at scale " << scale << '\n';
            return 1;
        }
        if (!preserves_native_label(scale)) {
            std::cerr << "The Society paint adapter did not preserve the shaped native label at scale " << scale << '\n';
            return 1;
        }
    }
}
