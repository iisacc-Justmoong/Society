#include "IosModelDrop.h"
#include "AppleModelSource.h"
#include "ModelImporter.h"

#include <QFileInfo>
#include <QPointer>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>

@interface SocietyModelDropDelegate : NSObject <UIDropInteractionDelegate> {
    QPointer<ModelImporter> _importer;
}
- (instancetype)initWithImporter:(ModelImporter *)importer;
@end

@implementation SocietyModelDropDelegate
- (instancetype)initWithImporter:(ModelImporter *)importer
{
    if ((self = [super init]))
        _importer = importer;
    return self;
}
- (BOOL)dropInteraction:(UIDropInteraction *)interaction canHandleSession:(id<UIDropSession>)session
{
    Q_UNUSED(interaction)
    if (!_importer || _importer->busy() || _importer->choosingFiles()
        || _importer->containerPath().isEmpty() || session.items.count == 0)
        return NO;
    for (UIDragItem *item in session.items) {
        const auto suffix = QFileInfo(QString::fromNSString(item.itemProvider.suggestedName)).suffix().toLower();
        if ((suffix != "safetensor" && suffix != "safetensors")
            || ![item.itemProvider hasItemConformingToTypeIdentifier:@"public.data"])
            return NO;
    }
    return YES;
}
- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction sessionDidUpdate:(id<UIDropSession>)session
{
    const BOOL accepted = [self dropInteraction:interaction canHandleSession:session];
    if (_importer)
        _importer->setNativeDragActive(accepted);
    return [[UIDropProposal alloc] initWithDropOperation:accepted ? UIDropOperationCopy : UIDropOperationForbidden];
}
- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session
{
    if (![self dropInteraction:interaction canHandleSession:session])
        return;
    QList<ModelImportSource> sources;
    for (UIDragItem *item in session.items)
        sources.append(appleModelSource(item.itemProvider));
    _importer->setNativeDragActive(false);
    _importer->importSources(sources);
}
- (void)dropInteraction:(UIDropInteraction *)interaction sessionDidExit:(id<UIDropSession>)session
{
    Q_UNUSED(interaction)
    Q_UNUSED(session)
    if (_importer)
        _importer->setNativeDragActive(false);
}
- (void)dropInteraction:(UIDropInteraction *)interaction sessionDidEnd:(id<UIDropSession>)session
{
    [self dropInteraction:interaction sessionDidExit:session];
}
@end

void attachIosModelDrop(QWindow *window, ModelImporter *importer)
{
    if (!window || !importer)
        return;
    // Qt's iOS QWindow::winId() is its native UIView.
    UIView *view = (__bridge UIView *)(reinterpret_cast<void *>(window->winId()));
    static char delegateKey;
    if (objc_getAssociatedObject(view, &delegateKey))
        return;
    SocietyModelDropDelegate *delegate = [[SocietyModelDropDelegate alloc] initWithImporter:importer];
    objc_setAssociatedObject(view, &delegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [view addInteraction:[[UIDropInteraction alloc] initWithDelegate:delegate]];
}
