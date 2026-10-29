#include "IosFileAccess.h"
#include "AppleModelSource.h"
#include "ModelImporter.h"
#include <QPointer>
#import <UIKit/UIKit.h>
#import <QuickLook/QuickLook.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

namespace {
UIViewController *presenter(QWindow *window = nullptr)
{
    UIWindow *nativeWindow = nil;
    if (window) {
        UIView *view = (__bridge UIView *)(reinterpret_cast<void *>(window->winId()));
        nativeWindow = view.window;
    }
    if (!nativeWindow) {
        for (UIWindowScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState != UISceneActivationStateForegroundActive)
                continue;
            for (UIWindow *candidate in scene.windows)
                if (candidate.isKeyWindow) nativeWindow = candidate;
        }
    }
    if (!nativeWindow) {
        // Qt 6.8 may use UIApplication without UIScene.
        for (UIWindow *candidate in UIApplication.sharedApplication.windows)
            if (candidate.isKeyWindow) nativeWindow = candidate;
    }
    UIViewController *controller = nativeWindow.rootViewController;
    while (controller.presentedViewController) controller = controller.presentedViewController;
    return controller;
}
}

@interface SocietyModelPickerDelegate : NSObject <UIDocumentPickerDelegate> {
@public
    QPointer<ModelImporter> importer;
}
@end
@implementation SocietyModelPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    Q_UNUSED(controller)
    if (!importer) return;
    QList<ModelImportSource> sources;
    for (NSURL *url in urls) sources.append(appleModelSource(url));
    importer->setChoosingFiles(false);
    importer->importSources(sources);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller)
    if (importer) importer->setChoosingFiles(false);
}
@end

bool presentIosModelPicker(QWindow *window, ModelImporter *importer)
{
    UIViewController *controller = presenter(window);
    if (!controller || !importer) return false;
    // Safetensors has no Apple system UTI. Imported UTIs also make Files' picker
    // recognize both spellings while ModelImporter remains the final validator.
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    for (NSString *suffix in @[@"safetensor", @"safetensors"]) {
        UTType *type = [UTType typeWithFilenameExtension:suffix conformingToType:UTTypeData];
        if (type) [types addObject:type];
    }
    if (!types.count) [types addObject:UTTypeData];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:types asCopy:NO];
    picker.allowsMultipleSelection = YES;
    SocietyModelPickerDelegate *delegate = [SocietyModelPickerDelegate new];
    delegate->importer = importer;
    picker.delegate = delegate;
    static char pickerOwner;
    objc_setAssociatedObject(picker, &pickerOwner, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    importer->setChoosingFiles(true);
    [controller presentViewController:picker animated:YES completion:nil];
    return true;
}

@interface SocietyPreviewSource : NSObject <QLPreviewControllerDataSource>
@property(nonatomic, strong) NSURL *url;
@end
@implementation SocietyPreviewSource
- (NSInteger)numberOfPreviewItemsInPreviewController:(QLPreviewController *)controller { Q_UNUSED(controller) return 1; }
- (id<QLPreviewItem>)previewController:(QLPreviewController *)controller previewItemAtIndex:(NSInteger)index
{ Q_UNUSED(controller) Q_UNUSED(index) return self.url; }
@end

bool previewIosFile(const QString &path)
{
    UIViewController *controller = presenter();
    NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
    if (!controller || ![QLPreviewController canPreviewItem:url]) return false;
    SocietyPreviewSource *source = [SocietyPreviewSource new];
    source.url = url;
    QLPreviewController *preview = [QLPreviewController new];
    preview.dataSource = source;
    static char previewOwner;
    objc_setAssociatedObject(preview, &previewOwner, source, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [controller presentViewController:preview animated:YES completion:nil];
    return true;
}

@interface SocietyImportBackgroundTask : NSObject {
@public
    QPointer<ModelImporter> importer;
    UIBackgroundTaskIdentifier task;
    BOOL expired;
}
- (void)update;
- (void)finish;
@end
@implementation SocietyImportBackgroundTask
- (instancetype)init { if ((self = [super init])) task = UIBackgroundTaskInvalid; return self; }
- (void)finish
{
    if (task == UIBackgroundTaskInvalid) return;
    const auto finished = task;
    task = UIBackgroundTaskInvalid;
    [UIApplication.sharedApplication endBackgroundTask:finished];
}
- (void)update
{
    if (!importer || !importer->busy()) { [self finish]; expired = NO; return; }
    if (task != UIBackgroundTaskInvalid || expired) return;
    __weak SocietyImportBackgroundTask *weakSelf = self;
    task = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Society model import" expirationHandler:^{
        SocietyImportBackgroundTask *owner = weakSelf;
        if (!owner) return;
        owner->expired = YES;
        if (owner->importer) owner->importer->cancel();
        [owner finish];
    }];
}
- (void)dealloc { [self finish]; }
@end

void observeIosImportLifecycle(ModelImporter *importer)
{
    SocietyImportBackgroundTask *background = [SocietyImportBackgroundTask new];
    background->importer = importer;
    QObject::connect(importer, &ModelImporter::stateChanged, importer, [background] { [background update]; });
}
