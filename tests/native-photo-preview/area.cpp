#include "Society/Native/PhotoAreaTracker.h"
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickTransform>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void events(){for(int i=0;i<4;++i)QCoreApplication::processEvents();}
struct IdentityTransform final : QQuickTransform { void applyTo(QMatrix4x4*)const override {} };
}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);
    QQuickWindow window;window.resize(600,400);
    auto* clip=new QQuickItem(window.contentItem());clip->setX(100);clip->setY(50);
    clip->setWidth(150);clip->setHeight(100);clip->setClip(true);
    auto* photo=new QQuickItem(clip);photo->setX(-20);photo->setY(10);photo->setWidth(220);photo->setHeight(80);
    auto* overlay=new QQuickItem(window.contentItem());
    // AccountViews is a permanently visible but empty overlay host. Its
    // presence alone must not prevent the photo surface from being mounted.
    auto* holder=new QQuickItem(overlay);
    auto* popup=new QQuickItem(overlay);popup->setFlag(QQuickItem::ItemHasContents);popup->setVisible(false);
    society::native::PhotoArea last;int changes=0;
    society::native::PhotoAreaTracker tracker(photo,&window,[&](const auto& area){last=area;++changes;},overlay);
    std::atomic_bool wrongThreadRejected=false;
    std::thread other([&]{try{tracker.refresh();}catch(const std::logic_error&){wrongThreadRejected=true;}});
    other.join();check(wrongThreadRejected,"A foreign thread changed photo-area state");
    window.show();events();
    if(qEnvironmentVariable("QT_SCALE_FACTOR")==QStringLiteral("1.25"))
        check(qFuzzyCompare(window.devicePixelRatio(),qreal(1.25)),"HiDPI test did not receive its requested scale");
    check(last.mode==society::native::PhotoArea::Mode::visible && last.bounds==QRectF(80,60,220,80) &&
          last.clip==QRectF(100,60,150,80) && last.deviceScale==window.devicePixelRatio(),
          "The photo area did not follow its parent, clip and display scale");
    const auto unchangedScale=changes;
    QEvent scaleEvent(QEvent::DevicePixelRatioChange);QCoreApplication::sendEvent(&window,&scaleEvent);events();
    check(changes==unchangedScale,"An unchanged display scale produced an unnecessary area update");
    const auto before=changes;
    clip->setX(110);photo->setX(-10);events();
    check(changes==before+1 && last.bounds==QRectF(100,60,220,80) && last.clip==QRectF(110,60,150,80),
          "A geometry change was not coalesced to the correct area");
    const auto unchanged=changes;
    photo->setOpacity(0);photo->setEnabled(false);events();
    check(changes==unchanged && last.mode==society::native::PhotoArea::Mode::visible,
          "Suppressing the legacy item also hid the native area");
    clip->setOpacity(0);events();
    check(last.mode==society::native::PhotoArea::Mode::hidden,"A transparent ancestor retained an input area");
    clip->setOpacity(1);events();
    check(last.mode==society::native::PhotoArea::Mode::visible,"An opaque ancestor did not restore the area");
    popup->setVisible(true);events();
    check(last.mode==society::native::PhotoArea::Mode::hidden,"A visible overlay did not retire the native area");
    popup->setVisible(false);events();
    check(last.mode==society::native::PhotoArea::Mode::visible,"Dismissing an overlay did not restore the area");
    auto* laterPopup=new QQuickItem;laterPopup->setFlag(QQuickItem::ItemHasContents);laterPopup->setParentItem(holder);events();
    check(last.mode==society::native::PhotoArea::Mode::hidden,"A newly added overlay was ignored");
    delete laterPopup;events();
    check(last.mode==society::native::PhotoArea::Mode::visible,"Removing an overlay did not restore the area");
    auto* inputBlocker=new QQuickItem;inputBlocker->setAcceptedMouseButtons(Qt::AllButtons);inputBlocker->setParentItem(holder);events();
    check(last.mode==society::native::PhotoArea::Mode::hidden,"An input-only overlay was ignored");
    delete inputBlocker;events();
    check(last.mode==society::native::PhotoArea::Mode::visible,"Removing an input overlay did not restore the area");
    photo->setVisible(false);events();
    check(last.mode==society::native::PhotoArea::Mode::hidden,"Hidden photo content remained active");
    photo->setVisible(true);clip->setRotation(10);events();
    check(last.mode==society::native::PhotoArea::Mode::unsupported,"A rotated parent was treated as an axis-aligned surface");
    clip->setRotation(0);events();
    check(last.mode==society::native::PhotoArea::Mode::visible,"An eligible area did not recover");
    {
        IdentityTransform transform;transform.appendToItem(photo);tracker.refresh();events();
        check(last.mode==society::native::PhotoArea::Mode::unsupported,"An added transform was not rejected on explicit refresh");
    }
    tracker.refresh();events();
    check(last.mode==society::native::PhotoArea::Mode::visible,"Removing a transform did not restore the area");
    auto* secondParent=new QQuickItem(window.contentItem());secondParent->setX(10);secondParent->setWidth(400);secondParent->setHeight(300);
    photo->setParentItem(secondParent);events();
    check(last.mode==society::native::PhotoArea::Mode::visible && last.bounds==QRectF(0,10,220,80),
          "Reparenting did not update the observed hierarchy");
    secondParent->setX(20);events();
    check(last.bounds==QRectF(10,10,220,80),"The new parent was not observed");
    delete photo;events();
    check(last.mode==society::native::PhotoArea::Mode::hidden,"Destroyed content retained its area");
}
