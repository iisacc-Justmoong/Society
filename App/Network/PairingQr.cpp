#include "PairingQr.h"
#include "ThirdParty/qrcodegen/qrcodegen.hpp"
#include <QPainter>
#include <QtMath>

QImage PairingQr::encode(const QString &text, int pixels) {
    if (text.isEmpty() || text.size() > 2048 || pixels < 1 || pixels > 32) return {};
    try {
        const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = (qr.getSize() + 8) * pixels;
        QImage image(size, size, QImage::Format_RGB32); image.fill(Qt::white); QPainter painter(&image);
        for (int y = 0; y < qr.getSize(); ++y) for (int x = 0; x < qr.getSize(); ++x)
            if (qr.getModule(x, y)) painter.fillRect((x + 4) * pixels, (y + 4) * pixels, pixels, pixels, Qt::black);
        return image;
    } catch (const std::exception &) { return {}; }
}
void PairingQr::setText(const QString &text) {
    if (text == m_text) return;
    m_text = text; m_modules = encode(text, 1); update(); emit textChanged();
}
void PairingQr::paint(QPainter *painter) {
    painter->fillRect(boundingRect(), Qt::white);
    if (m_modules.isNull()) return;
    const int scale = int(qMin(width(), height())) / m_modules.width();
    if (scale < 1) return;
    const int side = m_modules.width() * scale;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(QRectF(qFloor((width() - side) / 2), qFloor((height() - side) / 2), side, side), m_modules);
}
