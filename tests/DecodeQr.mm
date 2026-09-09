#include <QBuffer>
#include <QImage>
#include <QString>
#include "App/Network/AppleQrDecoder.h"
#import <CoreImage/CoreImage.h>

QString decodePairingQrImage(const QImage &image) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) return {};
    @autoreleasepool {
        NSData *data = [NSData dataWithBytes:bytes.constData() length:bytes.size()];
        CIImage *input = [CIImage imageWithData:data];
        CIDetector *detector = [CIDetector detectorOfType:CIDetectorTypeQRCode context:nil options:@{CIDetectorAccuracy: CIDetectorAccuracyHigh}];
        for (CIQRCodeFeature *feature in [detector featuresInImage:input])
            if (feature.messageString.length > 0) return QString::fromNSString(feature.messageString);
    }
    return {};
}

QString decodePairingQrCameraFrame(const QImage &image) {
    if (image.isNull()) return {};
    const auto source = image.convertToFormat(QImage::Format_ARGB32);
    CVPixelBufferRef pixels = nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault, source.width(), source.height(), kCVPixelFormatType_32BGRA,
                            (__bridge CFDictionaryRef)@{(NSString *)kCVPixelBufferIOSurfacePropertiesKey:@{}}, &pixels) != kCVReturnSuccess) return {};
    CVPixelBufferLockBaseAddress(pixels, 0);
    auto *base = static_cast<unsigned char *>(CVPixelBufferGetBaseAddress(pixels));
    for (int y = 0; y < source.height(); ++y)
        memcpy(base + y * CVPixelBufferGetBytesPerRow(pixels), source.constScanLine(y), source.width() * 4);
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    const auto result = decodeAppleQrFrame(pixels);
    CVPixelBufferRelease(pixels);
    return result.text;
}
