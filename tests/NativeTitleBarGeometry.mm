#include <QGuiApplication>
#include <QQuickWindow>
#include <QRectF>

#import <AppKit/AppKit.h>

// Inspect the real native window; QML material values alone cannot prove that
// an opaque AppKit surface is not covering the backdrop.
bool societyNativeWindowHasTranslucentBackdrop(QQuickWindow *window)
{
    if (!window || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return false;
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (!view || !view.window || view.window.opaque || view.window.alphaValue != 1.0
            || view.window.backgroundColor.alphaComponent != 0.0)
        return false;
    NSUInteger count = 0;
    for (NSView *sibling in view.superview.subviews) {
        if (![sibling.identifier isEqualToString:@"LVRSWindowBackdrop"])
            continue;
        if (![sibling isKindOfClass:NSVisualEffectView.class])
            return false;
        NSVisualEffectView *effect = static_cast<NSVisualEffectView *>(sibling);
        if (effect.blendingMode != NSVisualEffectBlendingModeBehindWindow
                || effect.material != NSVisualEffectMaterialUnderWindowBackground
                || effect.state != NSVisualEffectStateActive)
            return false;
        ++count;
    }
    return count == 1;
}

// Read AppKit's actual controls without calling LVRS's layout operation.
QRectF societyNativeTitleBarControlsRect(QQuickWindow *window)
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return {};
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSRect bounds = NSZeroRect;
    for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton *button = [view.window standardWindowButton:kind];
        if (!button)
            return {};
        const NSRect frame = [view convertRect:button.bounds fromView:button];
        bounds = NSIsEmptyRect(bounds) ? frame : NSUnionRect(bounds, frame);
    }
    return QRectF(bounds.origin.x,
        view.isFlipped ? bounds.origin.y : view.bounds.size.height - NSMaxY(bounds),
        bounds.size.width, bounds.size.height);
}
