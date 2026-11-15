#include "CoreDeadlineScheduler.h"
#include <iiSDKCore/Runtime/EventLoop.h>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <utility>

namespace society::native {
namespace {
using Clock=ii::runtime::Clock;
struct State final {
    QPointer<QObject> owner;
    std::shared_ptr<ii::runtime::EventLoop> loop;
    unsigned dispatching=0;
    void releaseIdle(){if(!dispatching&&loop&&loop->stats().pending==0)loop.reset();}
};
struct Ticket final {
    QPointer<QTimer> timer;
    ii::runtime::TaskToken token;
    Clock::time_point deadline;
};
int millisecondsUntil(Clock::time_point deadline){
    const auto remaining=std::max(Clock::duration::zero(),deadline-Clock::now());
    const auto rounded=std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
    return static_cast<int>(std::min<std::int64_t>(rounded,std::numeric_limits<int>::max()));
}
}
iisacc::accounts::presentation::ScheduleCooldown coreDeadlineScheduler(QObject* owner){
    auto state=std::make_shared<State>();state->owner=owner;
    if(owner){
        const std::weak_ptr<State> weak=state;
        QObject::connect(owner,&QObject::destroyed,[weak]{if(auto current=weak.lock())current->loop.reset();});
    }
    return [state](Clock::duration delay,std::function<void()> action)->std::optional<std::function<void()>>{
        if(!state->owner||state->owner->thread()!=QThread::currentThread()||!action||delay<Clock::duration::zero()||
           delay>std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(std::numeric_limits<int>::max())))return {};
        std::shared_ptr<Ticket> ticket;
        try{
            if(!state->loop)state->loop=std::make_shared<ii::runtime::EventLoop>(16);
            ticket=std::make_shared<Ticket>();ticket->deadline=Clock::now()+delay;
            auto* timer=new QTimer(state->owner);ticket->timer=timer;
            timer->setSingleShot(true);timer->setTimerType(Qt::PreciseTimer);
            const std::weak_ptr<Ticket> weakTicket=ticket;
            auto submitted=state->loop->at(ticket->deadline,[weakTicket,action=std::move(action)]()mutable{
                if(auto current=weakTicket.lock();current&&current->timer){current->timer->stop();current->timer->deleteLater();}
                action();
            });
            if(!submitted){delete timer;state->releaseIdle();return {};}
            ticket->token=submitted.token;
            const std::weak_ptr<State> weakState=state;
            QObject::connect(timer,&QTimer::timeout,state->owner,[weakState,ticket]{
                auto current=weakState.lock();if(!current||!current->loop)return;
                auto loop=current->loop;++current->dispatching;
                try{(void)loop->poll();}catch(...){}
                --current->dispatching;
                if(ticket->timer&&ticket->token.pending())ticket->timer->start(std::max(1,millisecondsUntil(ticket->deadline)));
                current->releaseIdle();
            });
            timer->start(millisecondsUntil(ticket->deadline));
            return std::function<void()>([weakState,ticket]{
                if(ticket->timer){ticket->timer->stop();ticket->timer->deleteLater();}
                (void)ticket->token.cancel();
                if(auto current=weakState.lock())current->releaseIdle();
            });
        }catch(...){
            if(ticket){if(ticket->timer){ticket->timer->stop();ticket->timer->deleteLater();}(void)ticket->token.cancel();}
            state->releaseIdle();return {};
        }
    };
}
}
