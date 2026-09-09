#include "QrScanner.h"
#include <QPointer>
#include <QQuickWindow>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

@interface SocietyQrCamera : UIViewController <AVCaptureMetadataOutputObjectsDelegate> {
    QPointer<QrScanner> _scanner;
    AVCaptureSession *_capture;
    AVCaptureVideoPreviewLayer *_preview;
    dispatch_queue_t _captureQueue;
    id _errorObserver;
    QString _startupError;
}
- (instancetype)initWithScanner:(QrScanner *)scanner;
- (void)endCapture;
@end

@implementation SocietyQrCamera
- (instancetype)initWithScanner:(QrScanner *)scanner {
    if ((self = [super init])) {
        _scanner = scanner;
        _captureQueue = dispatch_queue_create("com.iisacc.society.qr-camera", DISPATCH_QUEUE_SERIAL);
        self.title = @"Scan Society QR";
        self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel target:self action:@selector(cancelCapture)];
    }
    return self;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.blackColor;
    AVCaptureDevice *camera = [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionBack];
    NSError *error = nil;
    AVCaptureDeviceInput *input = camera ? [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error] : nil;
    _capture = [[AVCaptureSession alloc] init];
    AVCaptureMetadataOutput *output = [[AVCaptureMetadataOutput alloc] init];
    if (!input || ![_capture canAddInput:input]) { _startupError = QObject::tr("The camera is unavailable. Try again on an iPhone with a camera."); return; }
    [_capture addInput:input];
    if (![_capture canAddOutput:output]) { _startupError = QObject::tr("QR capture could not start."); return; }
    [_capture addOutput:output];
    if (![output.availableMetadataObjectTypes containsObject:AVMetadataObjectTypeQRCode]) { _startupError = QObject::tr("This camera does not support QR scanning."); return; }
    [output setMetadataObjectsDelegate:self queue:dispatch_get_main_queue()];
    output.metadataObjectTypes = @[AVMetadataObjectTypeQRCode];
    _preview = [AVCaptureVideoPreviewLayer layerWithSession:_capture];
    _preview.videoGravity = AVLayerVideoGravityResizeAspectFill;
    [self.view.layer addSublayer:_preview];
    const QPointer<QrScanner> guard = _scanner;
    _errorObserver = [NSNotificationCenter.defaultCenter addObserverForName:AVCaptureSessionRuntimeErrorNotification object:_capture queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
        if (guard) guard->failed(QObject::tr("Camera capture was interrupted. Try scanning again."));
    }];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (!_scanner || !_scanner->active()) return;
    // Finish presenting before reporting an error that dismisses this controller.
    if (!_startupError.isEmpty()) { _scanner->failed(_startupError); return; }
    AVCaptureSession *session = _capture;
    dispatch_async(_captureQueue, ^{ [session startRunning]; });
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews]; _preview.frame = self.view.bounds;
    const UIInterfaceOrientation orientation = self.view.window.windowScene.interfaceOrientation;
    if (_preview.connection.isVideoOrientationSupported) {
        switch (orientation) {
        case UIInterfaceOrientationLandscapeLeft: _preview.connection.videoOrientation = AVCaptureVideoOrientationLandscapeLeft; break;
        case UIInterfaceOrientationLandscapeRight: _preview.connection.videoOrientation = AVCaptureVideoOrientationLandscapeRight; break;
        case UIInterfaceOrientationPortraitUpsideDown: _preview.connection.videoOrientation = AVCaptureVideoOrientationPortraitUpsideDown; break;
        default: _preview.connection.videoOrientation = AVCaptureVideoOrientationPortrait; break;
        }
    }
}
- (void)metadataOutput:(AVCaptureMetadataOutput *)output didOutputMetadataObjects:(NSArray<__kindof AVMetadataObject *> *)objects fromConnection:(AVCaptureConnection *)connection {
    Q_UNUSED(output); Q_UNUSED(connection);
    if (!_scanner || !_scanner->active()) return;
    for (AVMetadataObject *object in objects) {
        if ([object isKindOfClass:AVMetadataMachineReadableCodeObject.class] && [object.type isEqualToString:AVMetadataObjectTypeQRCode]) {
            NSString *value = ((AVMetadataMachineReadableCodeObject *)object).stringValue;
            if (value.length > 0 && value.length <= 2048) { _scanner->captured(QString::fromNSString(value)); return; }
        }
    }
}
- (void)cancelCapture { if (_scanner) _scanner->stop(); }
- (void)endCapture {
    _scanner = nullptr;
    if (_errorObserver) { [NSNotificationCenter.defaultCenter removeObserver:_errorObserver]; _errorObserver = nil; }
    AVCaptureSession *session = _capture;
    if (session) dispatch_async(_captureQueue, ^{ [session stopRunning]; });
}
- (void)viewDidDisappear:(BOOL)animated { [super viewDidDisappear:animated]; if (_scanner) _scanner->stop(); }
- (void)dealloc { [self endCapture]; }
@end

void QrScanner::startNative(QQuickWindow *window) {
    const auto authorization = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (authorization == AVAuthorizationStatusNotDetermined) {
        const QPointer<QrScanner> guard(this); const QPointer<QQuickWindow> target(window);
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!guard || !guard->active()) return;
                if (granted && target) guard->startNative(target);
                else guard->failed(QObject::tr("Allow camera access in Settings to scan the desktop QR code."), !granted);
            });
        }]; return;
    }
    if (authorization != AVAuthorizationStatusAuthorized) { failed(tr("Allow camera access in Settings to scan the desktop QR code."), true); return; }
    UIView *view = (__bridge UIView *)(reinterpret_cast<void *>(window->winId()));
    UIWindow *nativeWindow = view.window;
    if (!nativeWindow) for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState != UISceneActivationStateForegroundActive || ![scene isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow *candidate in ((UIWindowScene *)scene).windows) if (candidate.isKeyWindow) nativeWindow = candidate;
    }
    UIViewController *presenter = nativeWindow.rootViewController;
    while (presenter.presentedViewController) presenter = presenter.presentedViewController;
    if (!presenter) { failed(tr("The camera window is unavailable.")); return; }
    SocietyQrCamera *camera = [[SocietyQrCamera alloc] initWithScanner:this];
    m_native = (__bridge_retained void *)camera;
    UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:camera];
    navigation.modalPresentationStyle = UIModalPresentationFullScreen;
    [presenter presentViewController:navigation animated:YES completion:nil];
}
void QrScanner::stopNative() {
    if (!m_native) return;
    SocietyQrCamera *camera = (__bridge_transfer SocietyQrCamera *)m_native; m_native = nullptr;
    [camera endCapture];
    [camera.navigationController dismissViewControllerAnimated:YES completion:nil];
}
void QrScanner::openSettings() {
    [UIApplication.sharedApplication openURL:[NSURL URLWithString:UIApplicationOpenSettingsURLString] options:@{} completionHandler:nil];
}
