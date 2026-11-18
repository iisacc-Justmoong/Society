#include "PhotoPreviewResolver.h"
#include <iiSDKCore>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace society::native {
struct PhotoPreviewResolver::State final {
    struct Key final {
        std::string source;
        std::uint32_t width=0, height=0;
        bool operator<(const Key& other) const {
            if (source!=other.source) return source<other.source;
            if (width!=other.width) return width<other.width;
            return height<other.height;
        }
    };
    struct Entry final {
        enum class Status { idle, loading, ready, failed };
        ii::core::Observable<std::uint64_t> revision{0};
        Image value;
        Status status=Status::idle;
        std::uint64_t generation=0, activeGeneration=0, used=0;
    };
    State(Limits value, Loader load) : limits(value), loader(std::move(load)), owner(std::this_thread::get_id()) {}
    void check() const {
        if (owner!=std::this_thread::get_id()) throw std::logic_error("Photo previews require the owner thread");
    }
    void touch(const std::shared_ptr<Entry>& entry) {
        if (sequence==std::numeric_limits<std::uint64_t>::max()) {
            sequence=0;
            for (const auto& [key, item] : entries) item->used=0;
        }
        entry->used=++sequence;
    }
    bool evictOne(const std::shared_ptr<Entry>& protectedEntry={}) {
        auto selected=entries.end();
        for (auto it=entries.begin();it!=entries.end();++it) {
            const auto& candidate=it->second;
            if (candidate==protectedEntry || candidate->status==Entry::Status::loading || candidate->revision.stats().observers) continue;
            if (selected==entries.end() || candidate->used<selected->second->used) selected=it;
        }
        if (selected==entries.end()) return false;
        if (selected->second->value) usedBytes-=selected->second->value->rgba().size();
        entries.erase(selected);
        return true;
    }
    void changed(const std::shared_ptr<Entry>& entry) {
        const auto revision=entry->revision.get();
        if (revision==std::numeric_limits<std::uint64_t>::max()) throw std::length_error("Photo preview revision exhausted");
        entry->revision.set(revision+1);
    }
    Limits limits;
    Loader loader;
    std::thread::id owner;
    std::map<Key,std::shared_ptr<Entry>> entries;
    std::size_t usedBytes=0;
    std::uint64_t sequence=0;
    bool alive=true;
};

PhotoPreviewResolver::PhotoPreviewResolver(Limits limits, Loader loader) {
    if (!limits.entries || !limits.bytes || !loader) throw std::invalid_argument("Photo previews need explicit limits and a loader");
    state_=std::make_shared<State>(limits,std::move(loader));
}
PhotoPreviewResolver::~PhotoPreviewResolver() {
    if (state_) { state_->alive=false; state_->entries.clear(); }
}
PhotoPreviewResolver::Image PhotoPreviewResolver::image(std::string_view source, std::uint32_t width, std::uint32_t height) const {
    auto state=state_;
    if (!state || !state->alive || source.empty() || !width || !height ||
        width>lvui::native::maximum_image_request_dimension || height>lvui::native::maximum_image_request_dimension ||
        std::uint64_t(width)*height>lvui::native::maximum_image_request_pixels) return {};
    state->check();
    State::Key key{std::string(source),width,height};
    auto found=state->entries.find(key);
    if (found==state->entries.end()) {
        while (state->entries.size()>=state->limits.entries) if (!state->evictOne()) return {};
        found=state->entries.emplace(key,std::make_shared<State::Entry>()).first;
    }
    auto entry=found->second;
    state->touch(entry);
    (void)entry->revision.get();
    if (entry->status!=State::Entry::Status::idle) return entry->value;
    if (entry->generation==std::numeric_limits<std::uint64_t>::max()) throw std::length_error("Photo preview generation exhausted");
    entry->status=State::Entry::Status::loading;
    const auto generation=++entry->generation;
    entry->activeGeneration=generation;
    const auto weakState=std::weak_ptr<State>(state);
    const auto weakEntry=std::weak_ptr<State::Entry>(entry);
    Completion complete=[weakState,weakEntry,generation](Image value) {
        auto owner=weakState.lock();auto current=weakEntry.lock();
        if (!owner || !current || !owner->alive || owner->owner!=std::this_thread::get_id() ||
            current->activeGeneration!=generation || current->status!=State::Entry::Status::loading) return;
        if (current->generation!=generation) {
            current->status=State::Entry::Status::idle;
            owner->changed(current);
            return;
        }
        const auto bytes=value?value->rgba().size():0;
        if (value && bytes<=owner->limits.bytes) {
            while (bytes>owner->limits.bytes-owner->usedBytes && owner->evictOne(current)) {}
            if (bytes<=owner->limits.bytes-owner->usedBytes) {
                owner->usedBytes+=bytes;current->value=std::move(value);current->status=State::Entry::Status::ready;
            } else current->status=State::Entry::Status::failed;
        } else current->status=State::Entry::Status::failed;
        owner->changed(current);
    };
    try { state->loader(key.source,width,height,complete); }
    catch (...) { complete({}); }
    return entry->value;
}
void PhotoPreviewResolver::invalidate(std::string_view source) {
    auto state=state_;
    if (!state || !state->alive || source.empty()) return;
    state->check();
    std::vector<std::shared_ptr<State::Entry>> changed;
    auto it=state->entries.lower_bound(State::Key{std::string(source),0,0});
    for (;it!=state->entries.end() && it->first.source==source;++it) {
        const auto& entry=it->second;
        if (entry->generation==std::numeric_limits<std::uint64_t>::max()) throw std::length_error("Photo preview generation exhausted");
        ++entry->generation;
        if (entry->value) state->usedBytes-=entry->value->rgba().size();
        entry->value.reset();
        if (entry->status!=State::Entry::Status::loading) entry->status=State::Entry::Status::idle;
        changed.push_back(entry);
    }
    for (const auto& entry : changed) {
        if (!state->alive) return;
        state->changed(entry);
    }
}
} // namespace society::native
