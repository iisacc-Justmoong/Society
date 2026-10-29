#include "QrScanner.h"
#include "AppleQrDecoder.h"
#include <QPointer>
#include <QQuickWindow>
#include <atomic>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

@interface SocietyQrCamera : UIViewController <AVCaptureMetadataOutputObjectsDelegate, AVCaptureVideoDataOutputSampleBufferDelegate> {
    QPointer<QrScanner> _scanner;
    AVCaptureSession *_capture;
    AVCaptureVideoPreviewLayer *_preview;
    AVCaptureVideoDataOutput *_frames;
    UIView *_guide;
    UILabel *_status;
    dispatch_queue_t _captureQueue;
    dispatch_queue_t _recognitionQueue;
    std::atomic_bool _ended;
    CFAbsoluteTime _lastFrame, _startedAt;
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
        _ended = false; _lastFrame = 0;
        _captureQueue = dispatch_queue_create("com.iisacc.society.qr-camera", DISPATCH_QUEUE_SERIAL);
        _recognitionQueue = dispatch_queue_create("com.iisacc.society.qr-recognition", DISPATCH_QUEUE_SERIAL);
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
    if (camera && [camera lockForConfiguration:&error]) {
        if ([camera isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus]) camera.focusMode = AVCaptureFocusModeContinuousAutoFocus;
        if (camera.isFocusPointOfInterestSupported) camera.focusPointOfInterest = CGPointMake(0.5, 0.5);
        if ([camera isExposureModeSupported:AVCaptureExposureModeContinuousAutoExposure]) camera.exposureMode = AVCaptureExposureModeContinuousAutoExposure;
        [camera unlockForConfiguration];
    }
    _capture = [[AVCaptureSession alloc] init];
    if ([_capture canSetSessionPreset:AVCaptureSessionPreset1920x1080]) _capture.sessionPreset = AVCaptureSessionPreset1920x1080;
    AVCaptureMetadataOutput *output = [[AVCaptureMetadataOutput alloc] init];
    if (!input || ![_capture canAddInput:input]) { _startupError = QObject::tr("The camera is unavailable. Try again on an iPhone with a camera."); return; }
    [_capture addInput:input];
    if (![_capture canAddOutput:output]) { _startupError = QObject::tr("QR capture could not start."); return; }
    [_capture addOutput:output];
    if (![output.availableMetadataObjectTypes containsObject:AVMetadataObjectTypeQRCode]) { _startupError = QObject::tr("This camera does not support QR scanning."); return; }
    [output setMetadataObjectsDelegate:self queue:dispatch_get_main_queue()];
    output.metadataObjectTypes = @[AVMetadataObjectTypeQRCode];
    // Metadata alone can miss a dense QR that Vision can decode from the same
    // photographed frame. Keep that fast path, but also inspect real pixels.
    _frames = [[AVCaptureVideoDataOutput alloc] init];
    _frames.videoSettings = @{(NSString *)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA)};
    _frames.alwaysDiscardsLateVideoFrames = YES;
    if (![_capture canAddOutput:_frames]) { _startupError = QObject::tr("QR image recognition could not start. Close the camera and try again."); return; }
    [_capture addOutput:_frames];
    [_frames setSampleBufferDelegate:self queue:_recognitionQueue];
    _preview = [AVCaptureVideoPreviewLayer layerWithSession:_capture];
    _preview.videoGravity = AVLayerVideoGravityResizeAspectFill;
    [self.view.layer addSublayer:_preview];
    _guide = [[UIView alloc] init]; _guide.userInteractionEnabled = NO;
    _guide.layer.borderWidth = 2; _guide.layer.borderColor = UIColor.whiteColor.CGColor; _guide.layer.cornerRadius = 16;
    [self.view addSubview:_guide];
    _status = [[UILabel alloc] init]; _status.textColor = UIColor.whiteColor;
    _status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    _status.numberOfLines = 0; _status.textAlignment = NSTextAlignmentCenter;
    _status.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:0.7];
    _status.layer.cornerRadius = 12; _status.clipsToBounds = YES;
    _status.text = @"Looking for a Society QR code…\nKeep the whole code inside the frame.";
    [self.view addSubview:_status];
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
    _startedAt = CFAbsoluteTimeGetCurrent();
    AVCaptureSession *session = _capture;
    dispatch_async(_captureQueue, ^{ [session startRunning]; });
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews]; _preview.frame = self.view.bounds;
    const CGRect safe = UIEdgeInsetsInsetRect(self.view.bounds, self.view.safeAreaInsets);
    const CGFloat side = MIN(300, MIN(safe.size.width - 48, safe.size.height - 180));
    _guide.frame = CGRectMake(CGRectGetMidX(safe) - side / 2, CGRectGetMidY(safe) - side / 2 - 30, side, side);
    _status.frame = CGRectMake(CGRectGetMinX(safe) + 20, CGRectGetMaxY(safe) - 116, safe.size.width - 40, 96);
    const UIInterfaceOrientation orientation = self.view.window.windowScene.interfaceOrientation;
    if (_preview.connection.isVideoOrientationSupported) {
        switch (orientation) {
        case UIInterfaceOrientationLandscapeLeft: _preview.connection.videoOrientation = AVCaptureVideoOrientationLandscapeLeft; break;
        case UIInterfaceOrientationLandscapeRight: _preview.connection.videoOrientation = AVCaptureVideoOrientationLandscapeRight; break;
        case UIInterfaceOrientationPortraitUpsideDown: _preview.connection.videoOrientation = AVCaptureVideoOrientationPortraitUpsideDown; break;
        default: _preview.connection.videoOrientation = AVCaptureVideoOrientationPortrait; break;
        }
    }
    AVCaptureConnection *frames = [_frames connectionWithMediaType:AVMediaTypeVideo];
    if (frames.isVideoOrientationSupported) frames.videoOrientation = _preview.connection.videoOrientation;
}
- (void)acceptText:(NSString *)value {
    if (!_scanner || !_scanner->active() || _ended.load()) return;
    if (value.length == 0 || value.length > 2048) {
        _status.text = @"QR detected, but the data is unreadable.\nMove back slightly or enlarge the desktop QR.";
        return;
    }
    _status.text = @"QR code read. Connecting to the desktop…";
    [[[UINotificationFeedbackGenerator alloc] init] notificationOccurred:UINotificationFeedbackTypeSuccess];
    _scanner->captured(QString::fromNSString(value));
}
- (void)metadataOutput:(AVCaptureMetadataOutput *)output didOutputMetadataObjects:(NSArray<__kindof AVMetadataObject *> *)objects fromConnection:(AVCaptureConnection *)connection {
    Q_UNUSED(output); Q_UNUSED(connection);
    if (!_scanner || !_scanner->active()) return;
    for (AVMetadataObject *object in objects) {
        if ([object isKindOfClass:AVMetadataMachineReadableCodeObject.class] && [object.type isEqualToString:AVMetadataObjectTypeQRCode]) {
            NSString *value = ((AVMetadataMachineReadableCodeObject *)object).stringValue;
            [self acceptText:value];
            if (value.length > 0 && value.length <= 2048) return;
        }
    }
}
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)buffer fromConnection:(AVCaptureConnection *)connection {
    Q_UNUSED(output); Q_UNUSED(connection);
    if (_ended.load()) return;
    const auto now = CFAbsoluteTimeGetCurrent();
    if (now - _lastFrame < 0.25) return; // At most four Vision requests/second; never queue old frames.
    _lastFrame = now;
    const auto result = decodeAppleQrFrame(CMSampleBufferGetImageBuffer(buffer));
    if (_ended.load()) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!_scanner || !_scanner->active() || _ended.load()) return;
        if (!result.text.isEmpty()) [self acceptText:result.text.toNSString()];
        else if (result.detected) [self acceptText:nil];
        else if (CFAbsoluteTimeGetCurrent() - _startedAt > 8)
            _status.text = @"Still looking for a QR code.\nMove back slightly, avoid glare, or show a new desktop QR.";
    });
}
- (void)cancelCapture { if (_scanner) _scanner->stop(); }
- (void)endCapture {
    if (_ended.exchange(true)) return;
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
