#include "Society/Native/NativeModalPresence.h"
#include <QGuiApplication>
#include <QPointer>
#include <QWindow>
#include <stdexcept>
#include <vector>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QWindow window;
    auto* presence=society::native::NativeModalPresence::forWindow(&window);
    check(presence && presence==society::native::NativeModalPresence::forWindow(&window),"The window did not share its modal state");
    std::vector<std::size_t> values;
    auto subscription=presence->count().observe([&](auto count,std::uint64_t){values.push_back(count);});
    auto first=presence->acquire(),second=presence->acquire();
    check(first && second && presence->count().get()==2,"Both modal leases were not admitted");
    first.reset();check(presence->count().get()==1,"Closing one modal cleared another");
    second.reset();check(presence->count().get()==0 && values==std::vector<std::size_t>{1,2,1,0},
                         "Modal retirement did not publish exact transitions");
    QPointer<society::native::NativeModalPresence> oldPresence(presence);
    delete presence;
    check(!oldPresence,"Deleting a window modal owner left it alive");
    auto* replacement=society::native::NativeModalPresence::forWindow(&window);
    check(replacement && replacement->count().get()==0,"A retired window modal owner was not recreated safely");
    auto* retiring=new society::native::NativeModalPresence;
    QPointer<society::native::NativeModalPresence> guarded(retiring);
    auto watch=retiring->count().observe([&](auto count,std::uint64_t){if(count==1)delete retiring;});
    auto late=retiring->acquire();
    check(!guarded && late,"Reentrant owner destruction was not handled");
    late.reset();
}
