#include "Society/Native/ExternalPhotoSurface.h"
#include <QGuiApplication>
#include <QWindow>
#include <memory>
#include <stdexcept>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);
    QWindow window;
    const society::native::PhotoArea visible{.mode=society::native::PhotoArea::Mode::visible,
        .bounds=QRectF(0,0,100,100),.clip=QRectF(0,0,100,100)};
    int mounted=0,ready=0,retired=0,closed=0;
    auto surface=std::make_shared<society::native::ExternalPhotoSurface>(&window,visible,[]{return true;},
        [&](auto&){++mounted;return true;},[&]{++ready;return true;},[&]{++retired;});
    check(!surface->start([&](bool){++closed;}),"A hidden owner admitted a native photo surface");
    check(surface->update({}),"An unattached surface did not accept a hidden-area update");
    surface->requestClose();
    check(mounted==0 && ready==0 && retired==0 && closed==0,"A rejected surface published lifecycle callbacks");
}
