#include "Society/Native/PhotoRegionGeometry.h"
#include <QRectF>
#include <limits>
#include <stdexcept>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    using society::native::photoRegionGeometry;
    const QRectF bounds(80,60,220,80), clip(100,60,150,80), parent(0,0,600,400);
    const auto flipped=photoRegionGeometry(bounds,clip,parent,true);
    check(flipped && flipped->outer==QRectF(100,60,150,80) && flipped->inner==QRectF(-20,0,220,80),
          "Flipped host coordinates did not retain the clipped content origin");
    const auto unflipped=photoRegionGeometry(bounds,clip,parent,false);
    check(unflipped && unflipped->outer==QRectF(100,260,150,80) && unflipped->inner==QRectF(-20,0,220,80),
          "Unflipped host coordinates were not converted correctly");
    const auto shifted=photoRegionGeometry(bounds,clip,QRectF(5,7,600,400),false);
    check(shifted && shifted->outer==QRectF(105,267,150,80),"The native parent bounds origin was ignored");
    const auto shiftedUIKit=photoRegionGeometry(bounds,clip,QRectF(5,7,600,400),true);
    check(shiftedUIKit && shiftedUIKit->outer==QRectF(105,67,150,80) &&
          shiftedUIKit->inner==QRectF(-20,0,220,80),"UIKit clipping lost the parent bounds origin");
    check(society::native::photoRegionScaleMatches(3,3),"Equal display scales were rejected");
    check(!society::native::photoRegionScaleMatches(2,3),"Different display scales were accepted");
    check(!society::native::photoRegionScaleMatches(0,3) &&
          !society::native::photoRegionScaleMatches(std::numeric_limits<double>::quiet_NaN(),3),
          "Invalid display scales were accepted");
    check(!photoRegionGeometry(bounds,QRectF(70,60,150,80),parent,true),"A clip outside the content was accepted");
    check(!photoRegionGeometry(bounds,QRectF(100,60,0,80),parent,true),"An empty clip was accepted");
    check(!photoRegionGeometry(bounds,clip,QRectF(0,0,std::numeric_limits<double>::infinity(),400),true),
          "A non-finite parent geometry was accepted");
}
