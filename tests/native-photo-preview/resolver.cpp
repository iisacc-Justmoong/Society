#include "Society/Native/PhotoPreviewResolver.h"
#include <iiGUI/Image.h>
#include <iiSDKCore/Core/Property/ObservableReadScope.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using society::native::PhotoPreviewResolver;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Image = std::shared_ptr<const ii::gui::RasterImage>;
struct Request final { std::string source; PhotoPreviewResolver::Completion done; };
Image image(unsigned char value) {
    auto result = ii::gui::rgba_image(1, 1, {value, 0, 0, 255});
    if (!result) throw std::runtime_error(result.error().message);
    return *result;
}
}

int main() {
    std::vector<Request> requests;
    auto load = [&](std::string source, std::uint32_t, std::uint32_t, PhotoPreviewResolver::Completion done) {
        requests.push_back({std::move(source), std::move(done)});
    };
    auto resolver = std::make_unique<PhotoPreviewResolver>(PhotoPreviewResolver::Limits{.entries=4, .bytes=16}, load);
    check(requests.empty(), "Preview work started without a request");
    check(!resolver->image("file:///oversized.jpg",lvui::native::maximum_image_request_dimension+1,1) && requests.empty(),
          "An oversized preview must be rejected before invoking the loader");
    std::vector<ii::core::ObservableRead> aReads, bReads;
    ii::core::ObservableReadScope::collect([&](auto read) { aReads.push_back(std::move(read)); }, [&] {
        check(!resolver->image("file:///a.jpg", 1, 1), "An unloaded image should be empty");
    });
    ii::core::ObservableReadScope::collect([&](auto read) { bReads.push_back(std::move(read)); }, [&] {
        check(!resolver->image("file:///b.jpg", 1, 1), "An unloaded image should be empty");
    });
    check(aReads.size()==1 && bReads.size()==1 && requests.size()==2, "Requests should bind per image");
    int aChanges=0, bChanges=0;
    auto aLease=aReads[0].subscribe([&] { ++aChanges; });
    auto bLease=bReads[0].subscribe([&] { ++bChanges; });
    check(!resolver->image("file:///a.jpg", 1, 1) && requests.size()==2, "A pending request must be coalesced");
    auto red=image(40); requests[0].done(red);
    check(aChanges==1 && bChanges==0, "Completion must update only the matching image");
    check(resolver->image("file:///a.jpg", 1, 1)==red && requests.size()==2, "The decoded image should be cached");

    resolver->invalidate("file:///b.jpg");
    check(aChanges==1 && bChanges==1, "Invalidation must update only the affected source");
    check(!resolver->image("file:///b.jpg", 1, 1) && requests.size()==2,
          "Invalidating an in-flight preview must not create concurrent duplicate work");
    requests[1].done(image(10));
    check(bChanges==2 && !resolver->image("file:///b.jpg", 1, 1) && requests.size()==3,
          "A stale completion should wake the current request without publishing its image");
    auto blue=image(20); requests[2].done(blue);
    check(bChanges==3 && resolver->image("file:///b.jpg", 1, 1)==blue, "The current completion was lost");

    check(!resolver->image("file:///late.jpg", 1, 1) && requests.size()==4, "The late request was not scheduled");
    auto late=std::move(requests.back().done);
    resolver.reset();
    late(image(30));

    std::vector<Request> boundedRequests;
    PhotoPreviewResolver bounded({.entries=1, .bytes=4}, [&](std::string source, std::uint32_t, std::uint32_t,
                                                               PhotoPreviewResolver::Completion done) {
        boundedRequests.push_back({std::move(source),std::move(done)});
    });
    std::vector<ii::core::ObservableRead> boundedReads;
    ii::core::ObservableReadScope::collect([&](auto read) { boundedReads.push_back(std::move(read)); }, [&] {
        (void)bounded.image("file:///one.jpg",1,1);
    });
    auto retained=boundedReads.front().subscribe([] {});
    boundedRequests[0].done(image(50));
    check(!bounded.image("file:///two.jpg",1,1) && boundedRequests.size()==1,
          "An observed preview must not be evicted beyond the explicit entry limit");
    retained.reset();
    check(!bounded.image("file:///two.jpg",1,1) && boundedRequests.size()==2,
          "An unobserved preview should free capacity for a new request");
    boundedRequests[1].done(image(60));
    check(bool(bounded.image("file:///two.jpg",1,1)), "The replacement preview did not fit the byte budget");

    std::vector<Request> reentrantRequests;
    auto reentrant=std::make_unique<PhotoPreviewResolver>(PhotoPreviewResolver::Limits{.entries=1,.bytes=4},
        [&](std::string source, std::uint32_t, std::uint32_t, PhotoPreviewResolver::Completion done) {
            reentrantRequests.push_back({std::move(source),std::move(done)});
        });
    std::vector<ii::core::ObservableRead> reentrantReads;
    ii::core::ObservableReadScope::collect([&](auto read) { reentrantReads.push_back(std::move(read)); }, [&] {
        (void)reentrant->image("file:///reentrant.jpg",1,1);
    });
    auto retirement=reentrantReads.front().subscribe([&] { reentrant.reset(); });
    reentrantRequests.front().done(image(70));
    check(!reentrant, "A preview owner could not be retired by its own notification");
}
