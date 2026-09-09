#include <QBuffer>
#include <QImage>
#include <QString>
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
