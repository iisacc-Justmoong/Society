#include "PhotoPreviewLoader.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace society::native {
namespace {
using Image=PhotoPreviewResolver::Image;
Image decode(const QString& path,std::uint32_t width,std::uint32_t height,PhotoPreviewLoader::Limits limits) {
    const QFileInfo file(path);
    if (!file.isFile() || file.size()<=0 || std::uint64_t(file.size())>limits.sourceBytes) return {};
    QImageReader reader(path);
    const auto source=reader.size();
    if (!source.isValid() || source.width()<=0 || source.height()<=0 ||
        std::uint64_t(source.width())*std::uint64_t(source.height())>limits.sourcePixels) return {};
    reader.setAutoTransform(true);
    reader.setScaledSize(source.scaled(QSize(int(width),int(height)),Qt::KeepAspectRatio));
    const auto decoded=reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (decoded.isNull() || decoded.width()<=0 || decoded.height()<=0) return {};
    const auto w=std::uint32_t(decoded.width()),h=std::uint32_t(decoded.height());
    if (w>lvui::native::maximum_image_request_dimension || h>lvui::native::maximum_image_request_dimension ||
        std::uint64_t(w)*h>lvui::native::maximum_image_request_pixels) return {};
    const auto rowBytes=std::size_t(w)*4;
    if (std::size_t(h)>std::numeric_limits<std::size_t>::max()/rowBytes) return {};
    std::vector<std::uint8_t> pixels(rowBytes*std::size_t(h));
    for (std::uint32_t row=0;row<h;++row)
        std::copy_n(decoded.constScanLine(int(row)),rowBytes,pixels.data()+std::size_t(row)*rowBytes);
    auto result=ii::gui::rgba_image(w,h,std::move(pixels));
    return result?std::move(*result):Image{};
}
}
PhotoPreviewLoader::PhotoPreviewLoader(Limits limits,QObject* parent):PhotoPreviewLoader(limits,decode,parent) {}
PhotoPreviewLoader::PhotoPreviewLoader(Limits limits,Decoder decoder,QObject* parent)
    :QObject(parent),limits_(limits),decoder_(std::move(decoder)) {
    if (!limits.sourceBytes || !limits.sourcePixels || !limits.maxInFlight || !decoder_)
        throw std::invalid_argument("Photo preview loading needs explicit source and work limits");
}
void PhotoPreviewLoader::load(std::string source,std::uint32_t width,std::uint32_t height,
                              PhotoPreviewResolver::Completion completion) {
    if (!completion) return;
    if (thread()!=QThread::currentThread()) throw std::logic_error("Photo preview loading requires the owner thread");
    if (source.size()>std::size_t(std::numeric_limits<qsizetype>::max())) {completion({});return;}
    const auto url=QUrl::fromEncoded(QByteArray(source.data(),qsizetype(source.size())));
    if (!url.isValid() || !url.isLocalFile() || (!url.host().isEmpty() && url.host()!=QStringLiteral("localhost")) ||
        !width || !height || width>lvui::native::maximum_image_request_dimension ||
        height>lvui::native::maximum_image_request_dimension || std::uint64_t(width)*height>lvui::native::maximum_image_request_pixels) {
        completion({});return;
    }
    const auto path=url.toLocalFile();
    if (path.isEmpty()) { completion({});return; }
    Pending request{path,width,height,std::move(completion)};
    if (!pumping_ && active_<limits_.maxInFlight && !pending_.empty()) {
        const QPointer<PhotoPreviewLoader> owner(this);
        pump();
        if (!owner) return;
    }
    if (active_>=limits_.maxInFlight || !pending_.empty() || pumping_) {
        if (pending_.size()>=limits_.maxQueued) {request.completion({});return;}
        pending_.push_back(std::move(request));pump();return;
    }
    start(std::move(request));
}
void PhotoPreviewLoader::start(Pending request) {
    auto callback=std::make_shared<PhotoPreviewResolver::Completion>(std::move(request.completion));
    auto* watcher=new QFutureWatcher<Image>(this);
    ++active_;
    const QPointer<PhotoPreviewLoader> owner(this);
    connect(watcher,&QFutureWatcher<Image>::finished,this,[owner,watcher,callback]() mutable {
        if (!owner) return;
        Image value;
        try { value=watcher->result(); } catch (...) {}
        watcher->deleteLater();
        --owner->active_;
        auto deliver=std::move(*callback);
        if (deliver) deliver(std::move(value));
        if (owner) owner->pump();
    });
    try {
        watcher->setFuture(QtConcurrent::run([path=request.path,width=request.width,height=request.height,
                                              limits=limits_,decoder=decoder_]{return decoder(path,width,height,limits);}));
    }
    catch (...) {
        watcher->disconnect(this);watcher->deleteLater();--active_;
        auto deliver=std::move(*callback);if (deliver) deliver({});
        if (owner) owner->pump();
    }
}
void PhotoPreviewLoader::pump() {
    if (pumping_) return;
    pumping_=true;
    const QPointer<PhotoPreviewLoader> owner(this);
    while (owner && active_<limits_.maxInFlight && !pending_.empty()) {
        auto request=std::move(pending_.front());pending_.pop_front();start(std::move(request));
    }
    if (owner) owner->pumping_=false;
}
} // namespace society::native
