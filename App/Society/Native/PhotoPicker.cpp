#include "PhotoPicker.h"
#include <exception>
#include <thread>
#include <utility>

namespace society::native {
struct PhotoPicker::State final {
    struct Pending final {ii::gui::FileDialogCompletion completion;bool delivered=false;};
    const std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<Pending> pending;
};
PhotoPicker::PhotoPicker():state_(std::make_shared<State>()){}
PhotoPicker::~PhotoPicker(){
    if(std::this_thread::get_id()!=state_->owner)std::terminate();
    state_->pending.reset();
}
ii::core::Result<void> PhotoPicker::open(Transport transport,ii::gui::FileDialogOptions options,
                                         ii::gui::FileDialogCompletion completion){
    auto state=state_;
    if(std::this_thread::get_id()!=state->owner)
        return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Photo picker requires its owner thread");
    if(!transport||!completion)return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Photo picker requires a transport and completion");
    if(state->pending)return ii::core::fail(ii::core::ErrorCode::busy,"Photo picker is already open");
    std::shared_ptr<State::Pending> pending;
    try{
        pending=std::make_shared<State::Pending>();pending->completion=std::move(completion);state->pending=pending;
        auto result=transport(std::move(options),[owner=std::weak_ptr(state),request=std::weak_ptr(pending)](auto selected)mutable{
            auto state=owner.lock();if(!state||std::this_thread::get_id()!=state->owner)return;
            auto pending=request.lock();if(!pending||state->pending!=pending)return;
            state->pending.reset();pending->delivered=true;auto completion=std::move(pending->completion);
            if(completion)completion(std::move(selected));
        });
        if(!result&&state->pending==pending)state->pending.reset();
        if(!result&&pending->delivered)return {};
        return result;
    }catch(...){
        if(state->pending==pending)state->pending.reset();
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot start photo picker",std::current_exception());
    }
}
ii::core::Result<void> PhotoPicker::cancel() {
    if(std::this_thread::get_id()!=state_->owner)
        return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Photo picker requires its owner thread");
    state_->pending.reset();return {};
}
}
