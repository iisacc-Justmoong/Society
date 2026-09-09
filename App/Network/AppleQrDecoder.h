#pragma once
#include <CoreVideo/CoreVideo.h>
#include <QString>

struct AppleQrResult {
    QString text;
    bool detected = false;
};

// The live camera and photographed-frame regression tests use this same decoder.
// Pixel buffers are read synchronously and never retained, saved or uploaded.
AppleQrResult decodeAppleQrFrame(CVPixelBufferRef pixels);
