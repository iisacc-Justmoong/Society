#include "Society/Native/PhotoClippedRegion.h"
#include <atomic>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace society::native;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct FakeWindows final : PhotoWindowDriver {
    struct Child {PhotoWindow parent;PhotoPixelRect frame;bool clipsChildren,visible;};
    PhotoWindowMetrics root{750,500,120};
    bool rootAlive=true,failSecond=false,failMove=false,failShow=false;
    std::uintptr_t next=2;
    int moves=0,shows=0,destroys=0,failMoveAt=0;
    std::map<PhotoWindow,Child> children;
    bool metrics(PhotoWindow window,PhotoWindowMetrics& value) noexcept override {
        if(window!=1 || !rootAlive || root.width<=0 || root.height<=0)return false;value=root;return true;
    }
    bool alive(PhotoWindow window) noexcept override {return window==1?rootAlive:children.contains(window);}
    bool parentIs(PhotoWindow window,PhotoWindow parent) noexcept override {
        return children.contains(window) && children[window].parent==parent && alive(parent);
    }
    PhotoWindow createChild(PhotoWindow parent,PhotoPixelRect frame,bool clips,bool visible) noexcept override {
        if(failSecond && parent!=1)return 0;
        const PhotoWindow id{next++};children.emplace(id,Child{parent,frame,clips,visible});return id;
    }
    bool move(PhotoWindow window,PhotoPixelRect frame) noexcept override {
        ++moves;if(failMove||moves==failMoveAt||!children.contains(window))return false;children[window].frame=frame;return true;
    }
    bool show(PhotoWindow window,bool visible) noexcept override {
        ++shows;if(failShow||!children.contains(window))return false;children[window].visible=visible;return true;
    }
    void destroy(PhotoWindow window) noexcept override {
        ++destroys;children.erase(window);
        for(auto it=children.begin();it!=children.end();)if(it->second.parent==window)it=children.erase(it);else ++it;
    }
};
int main(){
    const PhotoLogicalRect bounds{80,60,220,80},clip{100,60,150,80};
    FakeWindows system;
    {
        auto region=PhotoClippedRegion::create(system,1,bounds,clip,1.25);
        check(bool(region),"Valid clipped region was rejected");
        check(system.children.size()==2 && region->nativeParent()==3,"Region did not create one clip and content child");
        check(system.children[2].clipsChildren && !system.children[2].visible &&
              system.children[3].parent==2 && system.children[3].visible,"Region child styles or initial visibility were wrong");
        check(system.children[2].frame==PhotoPixelRect{125,75,188,100} &&
              system.children[3].frame==PhotoPixelRect{-25,0,275,100},"Region did not apply rounded clip geometry");
        system.root.dpi=144;
        check(!region->setVisible(true),"A changed DPI showed stale native geometry");
        system.root.dpi=120;system.root.width=100;
        check(!region->setVisible(true),"A resized parent showed geometry outside its client bounds");
        system.root.width=750;
        check(region->setVisible(true) && system.children[2].visible,"Region did not become visible");
        check(region->setVisible(true) && system.shows==1,"Unchanged visibility was sent twice");
        system.root.width=0;
        check(region->setVisible(false) && !system.children[2].visible,"An empty parent prevented the region from hiding");
        check(!region->setVisible(true),"An empty parent permitted the region to show");
        system.root.width=750;check(region->setVisible(true),"A restored parent did not permit the region to show");
        system.failShow=true;
        check(!region->setVisible(false) && system.children[2].visible,"A failed hide was reported as successful");
        system.failShow=false;
        check(region->update(bounds,clip,1.25) && system.moves==0,"Unchanged geometry was sent twice");
        check(region->update({80,60,240,80},clip,1.25) && system.moves==1 &&
              system.children[3].frame==PhotoPixelRect{-25,0,300,100},"Content resize was not applied selectively");
        std::atomic_bool rejected=false;
        std::thread other([&]{rejected=!region->update(bounds,clip,1.25);});other.join();
        check(rejected && system.moves==1,"A foreign thread changed the native region");
        check(!region->update(bounds,clip,1.5),"An incompatible Qt/native DPI was accepted");
        check(region->setVisible(false) && !system.children[2].visible,"Region did not hide");
        system.root={900,600,144};
        check(region->update(bounds,clip,1.5) && region->setVisible(true) &&
              system.children[2].frame==PhotoPixelRect{150,90,225,120} &&
              system.children[3].frame==PhotoPixelRect{-30,0,330,120},"DPI transition did not rebuild the clipped region");
    }
    check(system.children.empty() && system.destroys==2,"Destroying the clip did not retire both child windows");
    FakeWindows failed;failed.failSecond=true;
    check(!PhotoClippedRegion::create(failed,1,bounds,clip,1.25) && failed.children.empty() && failed.destroys==1,
          "Failed content creation leaked the clipping parent");
    FakeWindows dpi;
    check(!PhotoClippedRegion::create(dpi,1,bounds,clip,1.5) && dpi.children.empty(),"Mismatched DPI created native children");
    FakeWindows moved;
    auto invalidated=PhotoClippedRegion::create(moved,1,bounds,clip,1.25);
    check(bool(invalidated),"Move-failure fixture could not start");
    check(invalidated->setVisible(true),"Move-failure fixture could not show");
    moved.failMove=true;
    check(!invalidated->update({81,60,220,80},clip,1.25),"Failed native movement was reported as success");
    check(invalidated->setVisible(false),"Failed movement prevented the surface from hiding for fallback");
    moved.rootAlive=false;
    check(!invalidated->setVisible(true),"A destroyed parent accepted region visibility");
    FakeWindows partial;
    auto recovering=PhotoClippedRegion::create(partial,1,bounds,clip,1.25);
    check(recovering && recovering->setVisible(true),"Partial-move fixture could not start");
    const PhotoLogicalRect nextBounds{90,60,230,80},nextClip{110,60,150,80};
    partial.failMoveAt=2;
    check(!recovering->update(nextBounds,nextClip,1.25) && partial.moves==2,
          "A failed content movement was reported as successful");
    check(recovering->setVisible(false),"A partially moved region could not be hidden");
    partial.failMoveAt=0;
    check(recovering->update(nextBounds,nextClip,1.25) && partial.moves==3,
          "Retry did not preserve the already-applied clipping geometry");
    partial.children[3].parent=1;
    check(!recovering->update(nextBounds,nextClip,1.25) && !recovering->setVisible(true),
          "A reparented content window was accepted");
    check(recovering->setVisible(false) && !partial.children[3].visible,
          "A reparented content window remained visible during fallback");
    partial.children[3].parent=2;
    check(recovering->setVisible(true) && partial.children[3].visible,
          "A restored content child did not become visible again");
    partial.children[3].parent=1;
    recovering.reset();
    check(partial.children.empty() && partial.destroys==2,"A reparented owned content window leaked");
    FakeWindows recycled;
    auto oldRegion=PhotoClippedRegion::create(recycled,1,bounds,clip,1.25);
    const auto oldContent=oldRegion->nativeParent();
    recycled.children.erase(oldContent);
    const PhotoWindow newWindow{oldContent.native,99};
    recycled.children.emplace(newWindow,FakeWindows::Child{1,PhotoPixelRect{0,0,1,1},false,true});
    oldRegion.reset();
    check(recycled.children.contains(newWindow) && recycled.destroys==1,
          "A recycled native handle caused an unrelated window to be destroyed");
    FakeWindows missingContent;
    auto missing=PhotoClippedRegion::create(missingContent,1,bounds,clip,1.25);
    check(missing && missing->setVisible(true),"Missing-content fixture could not start");
    missingContent.children.erase(missing->nativeParent());
    check(missing->setVisible(false) && !missingContent.children[2].visible,
          "A destroyed content window prevented the clipping window from hiding");
    FakeWindows missingClip;
    auto orphan=PhotoClippedRegion::create(missingClip,1,bounds,clip,1.25);
    check(orphan && orphan->setVisible(true),"Missing-clip fixture could not start");
    missingClip.children[3].parent=1;missingClip.children.erase(2);
    check(orphan->setVisible(false) && !missingClip.children[3].visible,
          "A destroyed clip left its reparented content visible");
}
