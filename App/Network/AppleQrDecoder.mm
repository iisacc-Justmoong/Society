#include "AppleQrDecoder.h"
#import <Vision/Vision.h>

AppleQrResult decodeAppleQrFrame(CVPixelBufferRef pixels) {
    if (!pixels) return {};
    @autoreleasepool {
        VNDetectBarcodesRequest *request = [[VNDetectBarcodesRequest alloc] init];
        request.symbologies = @[VNBarcodeSymbologyQR];
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pixels options:@{}];
        NSError *error = nil;
        if (![handler performRequests:@[request] error:&error]) return {};
        AppleQrResult result;
        for (VNBarcodeObservation *observation in request.results) {
            result.detected = true;
            NSString *payload = observation.payloadStringValue;
            if (payload.length > 0 && payload.length <= 2048) {
                result.text = QString::fromNSString(payload);
                break;
            }
        }
        return result;
    }
}
