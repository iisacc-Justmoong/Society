#pragma once

#include "PhotoPreviewResolver.h"
#include <QObject>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace society::native {
// Product-level file/decoder service. Qt only supplies file decoding and the
// completion handoff; iiGUI paints the resulting raster on its GPU surface.
class PhotoPreviewLoader final : public QObject {
public:
    struct Limits final { std::uint64_t sourceBytes=0, sourcePixels=0; std::size_t maxInFlight=0, maxQueued=0; };
    using Decoder=std::function<PhotoPreviewResolver::Image(const QString&,std::uint32_t,std::uint32_t,Limits)>;
    explicit PhotoPreviewLoader(Limits, QObject* parent=nullptr);
    PhotoPreviewLoader(Limits, Decoder, QObject* parent=nullptr);
    void load(std::string source, std::uint32_t width, std::uint32_t height,
              PhotoPreviewResolver::Completion completion);
private:
    struct Pending final {
        QString path;
        std::uint32_t width=0,height=0;
        PhotoPreviewResolver::Completion completion;
    };
    void start(Pending);
    void pump();
    Limits limits_;
    Decoder decoder_;
    std::deque<Pending> pending_;
    std::size_t active_=0;
    bool pumping_=false;
};
} // namespace society::native
