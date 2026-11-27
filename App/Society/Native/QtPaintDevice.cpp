#include "QtPaintDevice.h"
#include <iiGUI>
#include <QCache>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
QRectF rect(const ii::gui::Rect &value) { return {value.x, value.y, value.width, value.height}; }
qreal encode(float value) {
    const auto linear = std::clamp(static_cast<double>(value), 0.0, 1.0);
    return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}
QColor color(const ii::gui::Color &value) {
    return QColor::fromRgbF(encode(value.red), encode(value.green), encode(value.blue),
                            std::clamp(static_cast<double>(value.alpha), 0.0, 1.0));
}
ii::core::Result<void> unsupported(const char *message) {
    return ii::core::fail(ii::core::ErrorCode::unsupported, message);
}

// Three separable box passes approximate the renderer's Gaussian shadow while
// keeping work linear in the size of the affected image, even at high DPI.
void blurShadow(std::vector<std::uint8_t> &pixels,int width,int height,int radius) {
    if (radius<=0) return;
    std::vector<std::uint8_t> scratch(pixels.size());
    const int divisor=2*radius+1;
    const auto pass=[&](const auto &source,auto &target,bool horizontal) {
        const int lines=horizontal?height:width, length=horizontal?width:height;
        for (int line=0;line<lines;++line) {
            const auto index=[&](int p) {return horizontal?std::size_t(line)*width+p:std::size_t(p)*width+line;};
            unsigned sum=0;
            for (int p=0;p<=radius && p<length;++p) sum+=source[index(p)];
            for (int p=0;p<length;++p) {
                target[index(p)]=std::uint8_t((sum+divisor/2)/divisor);
                if (p-radius>=0) sum-=source[index(p-radius)];
                if (p+radius+1<length) sum+=source[index(p+radius+1)];
            }
        }
    };
    for (int i=0;i<3;++i) {pass(pixels,scratch,true);pass(scratch,pixels,false);}
}

// Rasterize the iiGUI shadow geometry in its own logical coordinate space.
ii::core::Result<std::pair<QImage,QRectF>> shadowImage(const ii::gui::BoxShadow &shadow,double density) {
    try {
        if (!std::isfinite(density) || density<=0 || !std::isfinite(shadow.blur_radius) || shadow.blur_radius<0)
            return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid shadow scale or blur");
        const double sigma=shadow.blur_radius*density/3.0;
        if (!std::isfinite(sigma) || sigma>8192)
            return ii::core::fail(ii::core::ErrorCode::out_of_range,"Shadow blur exceeds the image budget");
        const int radius=std::max(0,int(std::lround((std::sqrt(4*sigma*sigma+1)-1)/2)));
        const double padding=double(3*radius+1)/density;
        const auto outer=rect(shadow.bounds).adjusted(-padding,-padding,padding,padding);
        const double wide=std::ceil(outer.width()*density),high=std::ceil(outer.height()*density);
        if (!(wide>0 && high>0) || wide>8192 || high>8192 || wide*high>16.0*1024*1024)
            return ii::core::fail(ii::core::ErrorCode::out_of_range,"Shadow raster exceeds the image budget");
        const int width=int(wide),height=int(high);
        QImage shape(width,height,QImage::Format_ARGB32_Premultiplied);
        if (shape.isNull()) return ii::core::fail(ii::core::ErrorCode::full,"Cannot allocate shadow mask");
        shape.setDevicePixelRatio(density);shape.fill(Qt::transparent);
        {
            QPainter painter(&shape);painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.translate(-outer.x(),-outer.y());
            if (shadow.mask) {
                const auto bytes=shadow.mask->rgba();
                QImage source(bytes.data(),int(shadow.mask->width()),int(shadow.mask->height()),
                              qsizetype(shadow.mask->width())*4,QImage::Format_RGBA8888);
                painter.drawImage(rect(shadow.bounds),source);
            } else {
                painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);
                painter.drawRoundedRect(rect(shadow.bounds),shadow.radius,shadow.radius);
            }
        }
        std::vector<std::uint8_t> alpha(std::size_t(width)*height);
        for (int y=0;y<height;++y) {
            const auto *line=reinterpret_cast<const QRgb *>(shape.constScanLine(y));
            for (int x=0;x<width;++x) alpha[std::size_t(y)*width+x]=std::uint8_t(qAlpha(line[x]));
        }
        blurShadow(alpha,width,height,radius);
        const auto tint=color(shadow.color);
        for (int y=0;y<height;++y) {
            auto *line=reinterpret_cast<QRgb *>(shape.scanLine(y));
            for (int x=0;x<width;++x) {
                const int a=std::lround(alpha[std::size_t(y)*width+x]*tint.alphaF());
                line[x]=qPremultiply(qRgba(tint.red(),tint.green(),tint.blue(),a));
            }
        }
        return std::pair{std::move(shape),outer};
    } catch (...) {
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot rasterize popup shadow",std::current_exception());
    }
}

// Paint LVUI's non-capturing, zero-blur fallback using the supplied material
// values and underlay; effects requiring a native backdrop remain explicit errors.
ii::core::Result<void> paintOpaqueMaterial(QPainter &painter,const ii::gui::BackdropMaterial &material) {
    if (material.capture || material.blur_radius!=0 || material.mask || material.background_scene || material.content_scene)
        return unsupported("Captured, blurred, masked or scene-backed materials require a native renderer");
    const auto bounds=rect(material.bounds);
    QPainterPath surface;surface.addRoundedRect(bounds,material.radius,material.radius);
    painter.save();
    painter.setClipPath(surface,Qt::IntersectClip);
    painter.fillPath(surface,color(material.base));
    if (material.underlay) {
        const auto bytes=material.underlay->rgba();
        QImage underlay(bytes.data(),int(material.underlay->width()),int(material.underlay->height()),
                        qsizetype(material.underlay->width())*4,QImage::Format_RGBA8888);
        if (material.underlay_tint) {
            underlay=underlay.copy();
            QPainter tint(&underlay);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(underlay.rect(),color(*material.underlay_tint));
        }
        painter.drawImage(rect(material.underlay_bounds),underlay);
    }
    if (material.tint.alpha>0) painter.fillPath(surface,color(material.tint));
    if (material.inner_highlight_opacity>0 && material.inner_highlight_offset_y!=0) {
        QTransform offset;offset.translate(0,material.inner_highlight_offset_y);
        const auto highlight=surface.subtracted(offset.map(surface));
        painter.fillPath(highlight,QColor::fromRgbF(1,1,1,std::clamp(double(material.inner_highlight_opacity),0.,1.)));
    }
    painter.restore();
    if (material.border_width>0 && material.border.alpha>0) {
        const auto half=material.border_width/2;
        const auto border=bounds.adjusted(half,half,-half,-half);
        painter.save();painter.setBrush(Qt::NoBrush);painter.setPen(QPen(color(material.border),material.border_width));
        painter.drawRoundedRect(border,std::max(0.0,double(material.radius-half)),std::max(0.0,double(material.radius-half)));
        painter.restore();
    }
    return {};
}
}

struct QtPaintDevice::TextRasters {
    struct Key {
        ii::gui::TextRun run;
        qreal density;
        bool operator==(const Key &) const = default;
        friend size_t qHash(const Key &key, size_t seed = 0) noexcept {
            const auto &r = key.run;
            return qHashMulti(seed, std::hash<std::string>{}(r.text), std::hash<std::string>{}(r.font_family),
                              r.bounds.width, r.bounds.height, r.font_size, r.font_weight, unsigned(r.alignment),
                              r.line_height, r.elide_right, r.underline, r.letter_spacing,
                              r.color.red, r.color.green, r.color.blue, r.color.alpha, key.density);
        }
    };
    struct Entry { QImage image; qreal scale, verticalOffset; };
    QCache<Key, Entry> entries{4 * 1024 * 1024};
    std::uint64_t fontRevision = 0;
};

QtPaintDevice::QtPaintDevice() = default;
QtPaintDevice::~QtPaintDevice() = default;
ii::gui::PaintCapabilities QtPaintDevice::capabilities() const noexcept { return {.backdrop = true, .shadow = true, .content_transform = true}; }

ii::core::Result<void> QtPaintDevice::begin_frame(const ii::gui::PaintFrameInfo &info) {
    if (m_painter || !info.pixel_width || !info.pixel_height || info.pixel_width > 8192 || info.pixel_height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid iiGUI frame");
    m_pending = QImage(static_cast<int>(info.pixel_width), static_cast<int>(info.pixel_height), QImage::Format_ARGB32_Premultiplied);
    if (m_pending.isNull()) return ii::core::fail(ii::core::ErrorCode::failed, "Cannot allocate iiGUI frame");
    m_pending.setDevicePixelRatio(info.scale);
    m_pending.fill(Qt::transparent);
    m_painter = std::make_unique<QPainter>(&m_pending);
    m_painter->setRenderHint(QPainter::Antialiasing);
    m_painter->setRenderHint(QPainter::SmoothPixmapTransform);
    return {};
}
ii::core::Result<void> QtPaintDevice::begin_scene(const ii::gui::PaintSceneInfo &info) {
    if (info.bounds) return unsupported("Nested scene sources are not supported by this adapter");
    return {};
}
ii::core::Result<void> QtPaintDevice::end_scene() { return {}; }
ii::core::Result<void> QtPaintDevice::begin_layer(const ii::gui::PaintLayerInfo &info) {
    m_painter->save();
    if (info.clip) m_painter->setClipRect(rect(*info.clip), Qt::IntersectClip);
    const auto &t = info.transform;
    m_painter->setOpacity(m_painter->opacity() * t.opacity);
    m_painter->translate(t.origin_x + t.offset_x, t.origin_y + t.offset_y);
    m_painter->scale(t.scale, t.scale);
    m_painter->translate(-t.origin_x, -t.origin_y);
    if (info.shadow && t.scale!=0) {
        auto image=shadowImage(*info.shadow,m_pending.devicePixelRatio()*std::abs(t.scale));
        if (!image) {m_painter->restore();return std::unexpected(image.error());}
        m_painter->drawImage(image->second,image->first);
    }
    if (info.backdrop) {
        if (auto result=paintOpaqueMaterial(*m_painter,*info.backdrop);!result) {
            m_painter->restore();return result;
        }
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::draw_panels(std::span<const ii::gui::Panel> panels) {
    for (const auto &panel : panels) {
        auto bounds = rect(panel.bounds);
        QLinearGradient gradient(bounds.topLeft(), bounds.bottomLeft());
        gradient.setColorAt(0, color(panel.top));
        gradient.setColorAt(1, color(panel.bottom));
        if (panel.stroke_width > 0) {
            const qreal half = panel.stroke_width / 2;
            bounds.adjust(half, half, -half, -half);
            m_painter->setBrush(Qt::NoBrush);
            m_painter->setPen(QPen(QBrush(gradient), panel.stroke_width));
            m_painter->drawRoundedRect(bounds, std::max(0.0, double(panel.radius) - half), std::max(0.0, double(panel.radius) - half));
        } else {
            m_painter->setPen(Qt::NoPen);
            m_painter->setBrush(QBrush(gradient));
            m_painter->drawRoundedRect(bounds, panel.radius, panel.radius);
        }
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::draw_text(std::span<const ii::gui::TextRun> runs) {
    const auto world = m_painter->worldTransform();
    const auto frameScale = m_pending.devicePixelRatio();
    const auto density = frameScale * world.m11();
    if (!(density > 0) || !std::isfinite(density)) return unsupported("Invalid native text scale");
    if (runs.empty()) return {};
    if (!m_textRasters) m_textRasters = std::make_unique<TextRasters>();
    auto &cache = *m_textRasters;
    const auto revision = ii::gui::FontDatabase::application()->revision();
    if (cache.fontRevision != revision) {
        cache.entries.clear();
        cache.fontRevision = revision;
    }
    for (const auto &run : runs) {
        const auto bounds = rect(run.bounds);
        const auto paintClip = run.clip ? bounds.intersected(rect(*run.clip)) : bounds;
        if (paintClip.isEmpty() || run.text.empty() || run.color.alpha == 0) continue;

        // Scrolling and clipping can reuse the same shaped raster.
        TextRasters::Key key{run, density};
        key.run.bounds.x = key.run.bounds.y = 0;
        key.run.clip.reset();
        const auto *entry = cache.entries.object(key);
        std::unique_ptr<TextRasters::Entry> transient;
        if (!entry) {
            const auto metrics = ii::gui::measure_wrapped_text_layout(key.run);
            if (!metrics) return std::unexpected(metrics.error());
            auto local = key.run;
            local.bounds.height = std::min(local.bounds.height, static_cast<float>(metrics->height));
            if (!(local.bounds.height > 0)) continue;
            // Extremely large transformed bounds fall back to a bounded, scaled raster.
            constexpr double maxPixels = 8192;
            auto scale = std::min({density, 8., maxPixels / local.bounds.width, maxPixels / local.bounds.height});
            if (local.bounds.width * scale > maxPixels || local.bounds.height * scale > maxPixels)
                scale = std::nextafter(scale, 0.);
            auto bitmap = ii::gui::render_text(std::move(local), scale);
            if (!bitmap) return std::unexpected(bitmap.error());
            if (bitmap->rgba.empty()) continue;
            const QImage rgba(std::as_const(bitmap->rgba).data(), static_cast<int>(bitmap->width),
                              static_cast<int>(bitmap->height), qsizetype(bitmap->width) * 4, QImage::Format_RGBA8888);
            auto image = rgba.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (image.isNull()) return ii::core::fail(ii::core::ErrorCode::full, "Cannot allocate native text presentation image");
            const auto height = std::min<double>(run.bounds.height, static_cast<float>(metrics->height));
            transient = std::make_unique<TextRasters::Entry>(TextRasters::Entry{
                std::move(image), scale, (run.bounds.height - height) / 2});
            entry = transient.get();
            const auto cost = std::max<qsizetype>(16 * 1024, entry->image.sizeInBytes() + key.run.text.size() + key.run.font_family.size());
            if (cost <= cache.entries.maxCost()) cache.entries.insert(std::move(key), transient.release(), cost);
        }

        auto x = bounds.x(), y = bounds.y() + entry->verticalOffset;
        m_painter->save();
        m_painter->setClipRect(paintClip, Qt::IntersectClip);
        if (entry->scale == density) {
            const auto align = [density](qreal position, qreal offset) {
                return (std::round(position * density + offset) - offset) / density;
            };
            x = align(x, world.dx() * frameScale);
            y = align(y, world.dy() * frameScale);
            m_painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        }
        m_painter->drawImage(QRectF{x, y, entry->image.width() / entry->scale, entry->image.height() / entry->scale},
                             entry->image, QRectF(entry->image.rect()));
        m_painter->restore();
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::draw_images(std::span<const ii::gui::ImageRun> runs) {
    for (const auto &run : runs) {
        if (!run.image || run.gradient || run.border_width > 0) return unsupported("Image effect is not supported by this adapter");
        const auto bytes = run.image->rgba();
        QImage source(bytes.data(), static_cast<int>(run.image->width()), static_cast<int>(run.image->height()),
                      static_cast<qsizetype>(run.image->width()) * 4, QImage::Format_RGBA8888);
        if (run.tint) {
            source = source.copy();
            QPainter tint(&source);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(source.rect(), color(*run.tint));
        }
        m_painter->save();
        m_painter->setOpacity(m_painter->opacity() * run.opacity);
        if (run.radius > 0) {
            QPainterPath clip;
            clip.addRoundedRect(rect(run.bounds), run.radius, run.radius);
            m_painter->setClipPath(clip, Qt::IntersectClip);
        }
        QRectF target = rect(run.bounds);
        QRectF sourceRect(run.source.x * source.width(), run.source.y * source.height(),
                          run.source.width * source.width(), run.source.height * source.height());
        const auto world = m_painter->worldTransform();
        if (world.type() <= QTransform::TxTranslate) {
            const auto scale = m_pending.devicePixelRatio();
            auto translated = run.bounds;
            translated.x += static_cast<float>(world.dx());
            translated.y += static_cast<float>(world.dy());
            if (const auto pixels = ii::gui::device_image_pixels(run, translated, scale)) {
                target = {pixels->x / scale - world.dx(), pixels->y / scale - world.dy(),
                          pixels->width / scale, pixels->height / scale};
                sourceRect = {qreal(pixels->source_x), qreal(pixels->source_y),
                              qreal(pixels->width), qreal(pixels->height)};
                m_painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
            }
        }
        m_painter->drawImage(target, source, sourceRect);
        m_painter->restore();
    }
    return {};
}
ii::core::Result<void> QtPaintDevice::end_layer() { m_painter->restore(); return {}; }
ii::core::Result<void> QtPaintDevice::present() { m_painter.reset(); m_presented = std::move(m_pending); return {}; }
void QtPaintDevice::abort_frame() noexcept { m_painter.reset(); m_pending = {}; }
