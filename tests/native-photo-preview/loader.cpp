#include "Society/Native/PhotoPreviewLoader.h"
#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <atomic>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Image=society::native::PhotoPreviewResolver::Image;
Image request(society::native::PhotoPreviewLoader& loader, const std::string& source,
              std::uint32_t width, std::uint32_t height) {
    QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    bool completed=false;Image result;
    loader.load(source,width,height,[&](Image value){completed=true;result=std::move(value);loop.quit();});
    if(!completed){timeout.start(5000);loop.exec();}
    check(completed,"Preview loader did not complete within the test deadline");
    return result;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir directory(QDir(QStringLiteral(SOCIETY_SOURCE_ROOT "/resources")).filePath(".photo-preview-test-XXXXXX"));
    check(directory.isValid(),"Could not create a temporary test resource directory");
    const auto path=directory.filePath("preview.png");
    QImage original(4,2,QImage::Format_RGBA8888);original.fill(QColor(255,0,0,255));
    check(original.save(path),"Could not create the temporary test image");
    const auto url=QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toStdString();
    society::native::PhotoPreviewLoader loader({.sourceBytes=1024*1024,.sourcePixels=1024,.maxInFlight=2,.maxQueued=4});
    auto decoded=request(loader,url,2,2);
    check(decoded && decoded->width()==2 && decoded->height()==1 && decoded->rgba().size()==8,
          "The local preview was not decoded and scaled as requested");
    check(!request(loader,"https://example.invalid/image.png",2,2),"The local preview loader accepted a remote URL");
    check(!request(loader,url,0,2),"The loader accepted an empty output dimension");
    society::native::PhotoPreviewLoader restricted({.sourceBytes=1,.sourcePixels=1024,.maxInFlight=2,.maxQueued=4});
    check(!request(restricted,url,2,2),"The loader exceeded the supplied source byte limit");
    bool afterRetirement=false;
    {
        auto retiring=std::make_unique<society::native::PhotoPreviewLoader>(society::native::PhotoPreviewLoader::Limits{1024*1024,1024,2,4});
        retiring->load(url,2,2,[&](Image){afterRetirement=true;});
    }
    QCoreApplication::processEvents();
    check(!afterRetirement,"A retired loader delivered a late completion");

    QSemaphore gate;std::atomic<int> active=0,peak=0;
    society::native::PhotoPreviewLoader bounded({.sourceBytes=1024,.sourcePixels=1024,.maxInFlight=1,.maxQueued=1},
        [&](const QString&,std::uint32_t,std::uint32_t,society::native::PhotoPreviewLoader::Limits)->Image {
            const auto now=++active;auto observed=peak.load();
            while(observed<now && !peak.compare_exchange_weak(observed,now)) {}
            gate.acquire();--active;
            auto result=ii::gui::rgba_image(1,1,{0,0,0,255});return result?*result:Image{};
        });
    int completed=0;bool overflowRejected=false;QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    auto done=[&](Image value){if(value && ++completed==2)loop.quit();};
    bounded.load(url,1,1,done);bounded.load(url,1,1,done);
    bounded.load(url,1,1,[&](Image value){overflowRejected=!value;});
    gate.release(2);
    check(overflowRejected,"The explicit pending-work bound was ignored");
    timeout.start(5000);loop.exec();
    check(completed==2 && peak.load()==1,"Preview decoding exceeded the active-work limit or lost queued work");

    QSemaphore fairGate;int delivered=0;bool rejectedDuringCompletion=false;QEventLoop fairLoop;QTimer fairTimeout;
    fairTimeout.setSingleShot(true);QObject::connect(&fairTimeout,&QTimer::timeout,&fairLoop,&QEventLoop::quit);
    society::native::PhotoPreviewLoader fair({.sourceBytes=1024,.sourcePixels=1024,.maxInFlight=1,.maxQueued=1},
        [&](const QString&,std::uint32_t,std::uint32_t,society::native::PhotoPreviewLoader::Limits)->Image {
            fairGate.acquire();auto result=ii::gui::rgba_image(1,1,{0,0,0,255});return result?*result:Image{};
        });
    auto finish=[&](Image value) { if(value && ++delivered==3) fairLoop.quit(); };
    fair.load(url,1,1,[&](Image value) {
        if(value)++delivered;
        fair.load(url,1,1,[&](Image next) {if(!next)rejectedDuringCompletion=true;finish(std::move(next));});
    });
    fair.load(url,1,1,finish);
    fairGate.release(3);fairTimeout.start(5000);fairLoop.exec();
    check(!rejectedDuringCompletion && delivered==3,"A completion rejected work even though it released a slot");
}
