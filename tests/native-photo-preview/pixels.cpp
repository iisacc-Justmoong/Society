#include "Society/Native/PhotoPixelGeometry.h"
#include <limits>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    using namespace society::native;
    const auto frames=photoPixelGeometry({80,60,220,80},{100,60,150,80},750,500,1.25);
    check(frames && frames->outer==PhotoPixelRect{125,75,188,100} &&
          frames->inner==PhotoPixelRect{-25,0,275,100},"Fractional-DPI edges did not preserve the clipped origin");
    const auto fractional=photoPixelGeometry({-.2,5.2,40.4,20.2},{0,5.2,30.1,20.2},200,100,1.5);
    check(fractional && fractional->outer==PhotoPixelRect{0,8,45,30} &&
          fractional->inner==PhotoPixelRect{0,0,60,30},"Fractional bounds were rounded inconsistently");
    const auto edge=photoPixelGeometry({0,0,100,100},{0,0,100,100},125,125,1.25);
    check(edge && edge->outer==PhotoPixelRect{0,0,125,125},"A clip touching the parent edge was rejected");
    check(!photoPixelGeometry({0,0,10,10},{-1,0,5,5},100,100,1),"A clip outside the content was accepted");
    check(!photoPixelGeometry({0,0,100,100},{0,0,100,100},99,100,1),"A clip outside the parent was accepted");
    check(!photoPixelGeometry({0,0,1e20,10},{0,0,10,10},100,100,1),"An overflowing content rectangle was accepted");
    check(!photoPixelGeometry({0,0,10,10},{0,0,10,10},100,100,std::numeric_limits<double>::infinity()),
          "A non-finite scale was accepted");
    check(!photoPixelGeometry({0,0,.1,.1},{0,0,.1,.1},100,100,1),"A collapsed pixel region was accepted");
    check(photoScaleCompatible(1.25,120) && photoScaleCompatible(1.5,144) &&
          !photoScaleCompatible(1.5,120) && !photoScaleCompatible(1,0),"DPI mismatch was not rejected");
}
