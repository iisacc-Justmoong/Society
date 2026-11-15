#include "NativeWorkspaceSurface.h"
#include "NativeWorkspace.generated.h"
#include "QtPaintDevice.h"
#include <iiGUI/PaintView.h>
#include <QFile>
#include <QClipboard>
#include <QDir>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QWheelEvent>
#include <mutex>

static void initializeWorkspaceIcons() { Q_INIT_RESOURCE(WorkspaceIcons); }
namespace {
using View = lvui_generated::NativeWorkspace;
ii::gui::WidgetKey key(int value) {
    using K = ii::gui::WidgetKey;
    switch (value) {
    case Qt::Key_Tab: return K::tab; case Qt::Key_Return: case Qt::Key_Enter: return K::enter;
    case Qt::Key_Space: return K::space; case Qt::Key_Escape: return K::escape;
    case Qt::Key_Left: return K::left; case Qt::Key_Right: return K::right;
    case Qt::Key_Up: return K::up; case Qt::Key_Down: return K::down;
    case Qt::Key_Home: return K::home; case Qt::Key_End: return K::end;
    case Qt::Key_Backspace: return K::backspace; case Qt::Key_Delete: return K::delete_forward;
    case Qt::Key_PageUp: return K::page_up; case Qt::Key_PageDown: return K::page_down;
    default: return K::none;
    }
}
}

struct NativeWorkspaceSurface::Impl final {
    std::shared_ptr<QtPaintDevice> device = std::make_shared<QtPaintDevice>();
    std::shared_ptr<ii::gui::PaintView> view;
    std::shared_ptr<View::Scope> scope;
    QString error;
    QImage image;
    std::mutex mutex;
};

NativeWorkspaceSurface::NativeWorkspaceSurface(QQuickItem *parent) : QQuickPaintedItem(parent), m(std::make_unique<Impl>()) {
    setAcceptHoverEvents(true); setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusScope, true); setFlag(ItemAcceptsInputMethod, true); setActiveFocusOnTab(true);
    initializeWorkspaceIcons();
    std::vector<lvui::IconDefinition> icons;
    const QDir iconDirectory(QStringLiteral(":/society/workspace"));
    for (const auto &fileName : iconDirectory.entryList({QStringLiteral("*.svg")}, QDir::Files)) {
        QFile file(iconDirectory.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) { m->error = tr("Could not load Workspace icon: %1").arg(fileName); return; }
        icons.push_back({fileName.left(fileName.size()-4).toStdString(), file.readAll().toStdString()});
    }
    auto catalog = lvui::IconCatalog::create(std::move(icons));
    if (!catalog) { m->error = QString::fromStdString(catalog.error().message); return; }
    const QPointer<NativeWorkspaceSurface> guard(this);
    auto result = ii::gui::PaintView::create(m->device, [&](ii::gui::WidgetBuilder &builder) {
        return View::build(builder, {}, [&](View::Scope &scope) {
            auto &vm = scope.backend_0;
            vm.iconSource = *catalog;
            vm.onGridSetSelection = [&scope](const std::vector<std::string> &paths) {
                scope.backend_1.configure({.mode=lvui::native::CollectionSelectionMode::multiple});
                lvui::native::CollectionSelectionState state;
                state.selected_keys=paths;
                if (!paths.empty()) { state.current_key=paths.back(); state.anchor_key=paths.front(); }
                (void)scope.backend_1.set_state(std::move(state));
            };
            vm.onFileMenuChanged = [guard](bool open) {
                if (!guard || !guard->m->view) return;
                auto menu=guard->m->view->find("workspace-file-menu");
                if (!menu) return;
                if (!open) { (void)menu->close_popup(); return; }
                auto anchor=guard->m->view->find("workspace-file-button");
                if (anchor) if (auto state=anchor->snapshot()) {
                    if (auto result=menu->open_at(state->bounds.x, state->bounds.y + state->bounds.height); result) return;
                }
                if (auto *model=guard->model()) {
                    model->fileMenuOpen.set(false); model->recordOperationError("The File menu could not be opened.");
                }
            };
            vm.onNavigate = [guard](auto page) {
                if (guard) QMetaObject::invokeMethod(guard, [guard, page] { if (guard) emit guard->pageRequested(int(page)); }, Qt::QueuedConnection);
            };
            vm.onGroup = [guard](const auto &kind, const auto &id) {
                if (guard) QMetaObject::invokeMethod(guard, [guard, kind, id] { if (guard) emit guard->groupRequested(QString::fromStdString(kind), QString::fromStdString(id)); }, Qt::QueuedConnection);
            };
            vm.onOpen = [guard](const auto &path) {
                if (guard) QMetaObject::invokeMethod(guard, [guard, path] { if (guard) emit guard->openRequested(QString::fromStdString(path)); }, Qt::QueuedConnection);
            };
            vm.onUp = [guard] { if (guard) QMetaObject::invokeMethod(guard, [guard] { if (guard) emit guard->upRequested(); }, Qt::QueuedConnection); };
        }, &m->scope);
    }, View::appearance());
    if (result) m->view = *result; else m->error = QString::fromStdString(result.error().message);
    connect(this, &QQuickItem::windowChanged, this, [this] { refresh(); });
}
NativeWorkspaceSurface::~NativeWorkspaceSurface() {
    disconnect(this, nullptr, this, nullptr);
    if (m->view) (void)m->view->close();
}
bool NativeWorkspaceSurface::ready() const { return m->view && m->error.isEmpty(); }
QString NativeWorkspaceSurface::errorString() const { return m->error; }
NativeWorkspaceViewModel *NativeWorkspaceSurface::model() const { return m->scope ? &m->scope->backend_0 : nullptr; }
bool NativeWorkspaceSurface::activateControl(const QString &id) {
    if (!m->view) return false;
    const auto control = m->view->find(id.toStdString());
    if (!control) { qWarning("Workspace control %s: %s", qPrintable(id), control.error().message.c_str()); return false; }
    const auto activated = control->activate();
    if (!activated) { qWarning("Workspace control %s: %s", qPrintable(id), activated.error().message.c_str()); return false; }
    refresh(); return true;
}
bool NativeWorkspaceSurface::focusControl(const QString &id) {
    if (!m->view) return false;
    const auto control = m->view->find(id.toStdString());
    return control && bool(control->focus());
}
QRectF NativeWorkspaceSurface::controlBounds(const QString &id) const {
    if (!m->view) return {};
    const auto control=m->view->find(id.toStdString());
    if (!control) return {};
    const auto snapshot=control->snapshot();
    if (!snapshot) return {};
    const auto &bounds=snapshot->bounds;
    return {bounds.x,bounds.y,bounds.width,bounds.height};
}
bool NativeWorkspaceSurface::controlOpened(const QString &id) const {
    if (!m->view) return false;
    const auto control=m->view->find(id.toStdString());
    if (!control) return false;
    const auto snapshot=control->snapshot();
    return snapshot && snapshot->opened;
}
void NativeWorkspaceSurface::refresh() {
    if (!m->view || width() <= 0 || height() <= 0) return;
    if (auto *vm = model()) vm->resize(width(), height());
    const auto result = m->view->paint({width(), height()}, window() ? window()->effectiveDevicePixelRatio() : 1.0);
    if (!result) { m->error = QString::fromStdString(result.error().message); return; }
    { std::lock_guard lock(m->mutex); m->image = m->device->image(); }
    m->error.clear(); update();
}
void NativeWorkspaceSurface::paint(QPainter *painter) { std::lock_guard lock(m->mutex); if (!m->image.isNull()) painter->drawImage(QPointF(), m->image); }
void NativeWorkspaceSurface::geometryChange(const QRectF &next, const QRectF &old) { QQuickPaintedItem::geometryChange(next, old); if (next.size() != old.size()) refresh(); }
void NativeWorkspaceSurface::hoverMoveEvent(QHoverEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=e->position().x(),.y=e->position().y()}); refresh(); e->accept(); }
void NativeWorkspaceSurface::hoverLeaveEvent(QHoverEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_leave}); refresh(); e->accept(); }
void NativeWorkspaceSurface::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus(Qt::MouseFocusReason);
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=e->position().x(),.y=e->position().y(),
        .shift=bool(e->modifiers() & Qt::ShiftModifier),.control=bool(e->modifiers() & Qt::ControlModifier),
        .alt=bool(e->modifiers() & Qt::AltModifier),.meta=bool(e->modifiers() & Qt::MetaModifier),.buttons=1});
    refresh(); e->accept();
}
void NativeWorkspaceSurface::mouseReleaseEvent(QMouseEvent *e) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=e->position().x(),.y=e->position().y(),
        .shift=bool(e->modifiers() & Qt::ShiftModifier),.control=bool(e->modifiers() & Qt::ControlModifier),
        .alt=bool(e->modifiers() & Qt::AltModifier),.meta=bool(e->modifiers() & Qt::MetaModifier)});
    refresh(); e->accept();
}
void NativeWorkspaceSurface::mouseDoubleClickEvent(QMouseEvent *e) {
    if (m->view) if (auto table = m->view->find("workspace-table")) if (auto s = table->snapshot()) {
        const auto &b = s->bounds;
        if (QRectF(b.x, b.y, b.width, b.height).contains(e->position()) && model()) model()->openSelected();
    }
    e->accept();
}
void NativeWorkspaceSurface::mouseUngrabEvent() { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_cancel}); refresh(); }
void NativeWorkspaceSurface::wheelEvent(QWheelEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::scroll,.x=e->position().x(),.y=e->position().y(),.scroll_y=double(e->angleDelta().y())/8.0}); refresh(); e->accept(); }
void NativeWorkspaceSurface::keyPressEvent(QKeyEvent *e) {
    if (!m->view) { e->ignore(); return; }
    using K = ii::gui::WidgetKey;
    if (e->matches(QKeySequence::Paste)) {
        const auto text = QGuiApplication::clipboard()->text();
        if (!text.isEmpty()) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=text.toStdString()});
        refresh(); e->accept(); return;
    }
    auto k = key(e->key());
    if (e->matches(QKeySequence::SelectAll)) k = K::select_all;
    else if (e->matches(QKeySequence::Copy)) k = K::copy;
    else if (e->matches(QKeySequence::Cut)) k = K::cut;
    else if (e->matches(QKeySequence::Undo)) k = K::undo;
    else if (e->matches(QKeySequence::Redo)) k = K::redo;
    if (k != K::none) {
        (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_down,.key=k,.shift=bool(e->modifiers() & Qt::ShiftModifier),.control=bool(e->modifiers() & Qt::ControlModifier),.meta=bool(e->modifiers() & Qt::MetaModifier)});
        // Native input fields receive printable characters through text events;
        // retain the key event as well so Space still activates buttons.
        if (k == K::space && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
            (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=" "});
    }
    else if (!e->text().isEmpty() && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=e->text().toStdString()});
    else { e->ignore(); return; }
    refresh(); e->accept();
}
void NativeWorkspaceSurface::keyReleaseEvent(QKeyEvent *e) { const auto k=key(e->key()); if (m->view && k!=ii::gui::WidgetKey::none) (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_up,.key=k}); refresh(); e->accept(); }
void NativeWorkspaceSurface::inputMethodEvent(QInputMethodEvent *e) {
    if (m->view) {
        if (!e->commitString().isEmpty()) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=e->commitString().toStdString()});
        (void)m->view->input({.kind=ii::gui::WidgetInputKind::composition,.text=e->preeditString().toStdString()});
    }
    refresh(); e->accept();
}
QVariant NativeWorkspaceSurface::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) return true;
    return QQuickPaintedItem::inputMethodQuery(query);
}
void NativeWorkspaceSurface::focusOutEvent(QFocusEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::focus_lost}); refresh(); QQuickPaintedItem::focusOutEvent(e); }
