#include "NativeAppsSurface.h"
#include "NativeApps.generated.h"
#include "NativeAppsViewModel.h"
#include "QtPaintDevice.h"
#include "Society/Environment/AppInstallController.h"

#include <iiGUI/PaintView.h>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHash>
#include <QHoverEvent>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QWheelEvent>
#include <algorithm>
#include <mutex>

static void initializeAppsAssets() { Q_INIT_RESOURCE(NativeAppsAssets); }
namespace {
using View = lvui_generated::NativeApps;
// Translate only keys represented by iiGUI; printable input is sent separately.
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
// Resource images are copied into iiGUI-owned RGBA storage; no QImage memory is borrowed.
std::shared_ptr<const ii::gui::RasterImage> icon(const QString &name) {
    auto image = QImage(QStringLiteral(":/society/apps/") + name + QStringLiteral(".png")).convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return {};
    const auto width = std::uint32_t(image.width()), height = std::uint32_t(image.height());
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
        std::copy_n(image.constScanLine(int(y)), std::size_t(width) * 4, pixels.data() + std::size_t(y) * width * 4);
    auto result = ii::gui::rgba_image(width, height, std::move(pixels));
    return result ? std::move(*result) : std::shared_ptr<const ii::gui::RasterImage>{};
}
}

struct NativeAppsSurface::Impl final {
    std::shared_ptr<QtPaintDevice> device = std::make_shared<QtPaintDevice>();
    std::shared_ptr<ii::gui::PaintView> view;
    std::shared_ptr<View::Scope> scope;
    QHash<QString, std::shared_ptr<const ii::gui::RasterImage>> icons;
    QPointer<QObject> catalogue;
    QPointer<QObject> environment;
    QHash<QString, QVariantMap> webEntries;
    AppInstallController *installer = nullptr;
    QString error;
    QImage image;
    std::mutex mutex;
};

NativeAppsSurface::NativeAppsSurface(QQuickItem *parent) : QQuickPaintedItem(parent), m(std::make_unique<Impl>()) {
    setAcceptHoverEvents(true); setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusScope, true); setFlag(ItemAcceptsInputMethod, true); setActiveFocusOnTab(true);
    initializeAppsAssets();
    m->installer = new AppInstallController(this);
    connect(m->installer, &AppInstallController::activityChanged, this, [this](const QString &text, bool busy) {
        if (model()) { model()->setActivity(text.toStdString(), busy); refresh(); }
    });
    connect(m->installer, &AppInstallController::handoffFinished, this, [this] {
        if (m->catalogue) QMetaObject::invokeMethod(m->catalogue, "refresh");
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m->catalogue) QMetaObject::invokeMethod(m->catalogue, "refresh");
    });
    std::vector<lvui::IconDefinition> icons;
    const QDir directory(QStringLiteral(":/society/apps"));
    for (const auto &name : directory.entryList({QStringLiteral("*.png")}, QDir::Files)) {
        const auto product = name.left(name.size() - 4);
        auto image = icon(product);
        if (!image) { m->error = tr("Could not load an Apps product icon."); return; }
        m->icons.insert(product, std::move(image));
    }
    if (m->icons.isEmpty()) { m->error = tr("Could not load the Apps product icons."); return; }
    for (const auto &name : directory.entryList({QStringLiteral("*.svg")}, QDir::Files)) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) { m->error = tr("Could not load an Apps navigation icon."); return; }
        icons.push_back({name.left(name.size() - 4).toStdString(), file.readAll().toStdString()});
    }
    auto catalog = lvui::IconCatalog::create(std::move(icons));
    if (!catalog) { m->error = QString::fromStdString(catalog.error().message); return; }
    const QPointer<NativeAppsSurface> guard(this);
    auto result = ii::gui::PaintView::create(m->device, [&](ii::gui::WidgetBuilder &builder) {
        return View::build(builder, {}, [&](View::Scope &scope) {
            scope.backend_0.iconSource = *catalog;
            scope.backend_0.onAction = [guard](const std::string &id) {
                if (!guard || !guard->model()) return;
                const auto product = QString::fromStdString(id);
                const auto action = QString::fromStdString(guard->model()->detailAction.get());
                QMetaObject::invokeMethod(guard, [guard, product, action] {
                    if (guard) guard->requestAction(product, action);
                }, Qt::QueuedConnection);
            };
        }, &m->scope);
    }, View::appearance());
    if (!result) { m->error = QString::fromStdString(result.error().message); return; }
    m->view = *result;
    connect(this, &QQuickItem::windowChanged, this, [this] { refresh(); });
}
void NativeAppsSurface::requestAction(const QString &id, const QString &displayedAction) {
    if (!m->catalogue || !model()) return;
    if (id.startsWith(QLatin1String("web:"))) {
        const auto entry = m->webEntries.value(id);
        if (!m->environment || entry.isEmpty() || displayedAction != QLatin1String("Open web") ||
            !QMetaObject::invokeMethod(m->environment, "openApp", Q_ARG(QVariant, QVariant(entry)))) {
            model()->setNotice("The web app could not be opened.");
            refresh();
        }
        return;
    }
#if defined(SOCIETY_NATIVE_INSTALLER_AVAILABLE)
    if (displayedAction == tr("Install")) {
        QVariantMap release;
        if (QMetaObject::invokeMethod(m->catalogue, "installRelease", Q_RETURN_ARG(QVariantMap, release),
                                      Q_ARG(QString, id)) && !release.isEmpty()) {
            (void)m->installer->start(release);
        } else {
            model()->setActivity("The app or its release changed. Review the refreshed catalogue.", false);
            synchronizeCatalogue();
        }
        return;
    }
#endif
    QMetaObject::invokeMethod(m->catalogue, "refresh");
    bool stillMatches = false;
    for (const auto &value : m->catalogue->property("apps").toList()) {
        const auto app = value.toMap();
        if (app.value("id").toString() == id && app.value("actionEnabled").toBool() && app.value("action").toString() == displayedAction) {
            stillMatches = true; break;
        }
    }
    bool opened = false;
    const bool called = stillMatches && QMetaObject::invokeMethod(m->catalogue, "open", Q_RETURN_ARG(bool, opened), Q_ARG(QString, id));
    if (!called || !opened) model()->setActivity("The requested action could not be started.", false);
    synchronizeCatalogue();
}
NativeAppsSurface::~NativeAppsSurface() { disconnect(this, nullptr, this, nullptr); if (m->view) (void)m->view->close(); }
bool NativeAppsSurface::ready() const { return m->view && m->error.isEmpty(); }
QString NativeAppsSurface::errorString() const { return m->error; }
NativeAppsViewModel *NativeAppsSurface::model() const { return m->scope ? &m->scope->backend_0 : nullptr; }
void NativeAppsSurface::setCatalogue(QObject *catalogue) {
    if (m->catalogue) disconnect(m->catalogue, nullptr, this, nullptr);
    m->catalogue = catalogue;
    if (catalogue) connect(catalogue, SIGNAL(changed()), this, SLOT(synchronizeCatalogue()));
    synchronizeCatalogue();
}
void NativeAppsSurface::setEnvironment(QObject *environment) {
    if (m->environment) disconnect(m->environment, nullptr, this, nullptr);
    m->environment = environment;
    if (environment) {
        connect(environment, SIGNAL(webEntriesChanged()), this, SLOT(synchronizeCatalogue()));
        connect(environment, SIGNAL(customWebAppsChanged()), this, SLOT(synchronizeCatalogue()));
    }
    synchronizeCatalogue();
}
void NativeAppsSurface::synchronizeCatalogue() {
    if (!model()) return;
    std::vector<NativeAppsItem> apps;
    std::vector<NativeAppsItem> webApps;
    m->webEntries.clear();
    if (m->environment) {
        const auto entries = [this](const char *name) {
            auto value = m->environment->property(name);
            if (value.metaType() == QMetaType::fromType<QJSValue>()) value = value.value<QJSValue>().toVariant();
            return value.toList();
        };
        auto values = entries("webEntries"); values.append(entries("customWebApps"));
        for (const auto &value : values) {
            const auto app = value.toMap();
            const auto key = QStringLiteral("web:") + app.value("id").toString();
            if (key == QLatin1String("web:") || m->webEntries.contains(key)) continue;
            const QUrl url(app.value("url").toString(), QUrl::StrictMode);
            const bool available = url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty() && url.userInfo().isEmpty();
            m->webEntries.insert(key, app);
            webApps.push_back({key.toStdString(), app.value("name").toString().toStdString(),
                app.value("description").toString().toStdString(), app.value("status").toString().toStdString(),
                "Open web", app.value("publisher").toString().toStdString(), false, available});
        }
    }
    model()->setWebApps(std::move(webApps), bool(m->environment));
    if (m->catalogue) {
        for (const auto &value : m->catalogue->property("apps").toList()) {
            const auto app = value.toMap();
            const auto id = app.value("id").toString();
            // The legacy catalogue includes design-only product placeholders.
            // Show only observed installations or published actions for other apps.
            if (id == QLatin1String("society") ||
                (!app.value("installed").toBool() && !app.value("actionEnabled").toBool())) continue;
            auto image = m->icons.value(id.section(QLatin1Char('.'), 0, 0));
            auto action = app.value("action").toString();
#if defined(SOCIETY_NATIVE_INSTALLER_AVAILABLE)
            if (!app.value("installed").toBool() && !id.endsWith(QLatin1String(".installer")) && action == QLatin1String("Download"))
                action = tr("Install");
#endif
            apps.push_back({id.toStdString(), app.value("name").toString().toStdString(),
                app.value("description").toString().toStdString(), app.value("status").toString().toStdString(),
                action.toStdString(), app.value("publisher").toString().toStdString(),
                app.value("installed").toBool(), app.value("actionEnabled").toBool(), std::move(image)});
        }
    }
    model()->setApps(std::move(apps)); refresh();
}
void NativeAppsSurface::refresh() {
    if (!m->view || width() <= 0 || height() <= 0) return;
    if (auto *vm = model()) vm->resize(width(), height());
    auto result = m->view->paint({width(), height()}, window() ? window()->effectiveDevicePixelRatio() : 1.0);
    if (!result) { m->error = QString::fromStdString(result.error().message); return; }
    { std::lock_guard lock(m->mutex); m->image = m->device->image(); }
    m->error.clear(); update();
}
void NativeAppsSurface::paint(QPainter *painter) { std::lock_guard lock(m->mutex); if (!m->image.isNull()) painter->drawImage(QPointF(), m->image); }
void NativeAppsSurface::geometryChange(const QRectF &n, const QRectF &o) { QQuickPaintedItem::geometryChange(n, o); if (n.size() != o.size()) refresh(); }
void NativeAppsSurface::hoverMoveEvent(QHoverEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=e->position().x(),.y=e->position().y()}); refresh(); e->accept(); }
void NativeAppsSurface::hoverLeaveEvent(QHoverEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_leave}); refresh(); e->accept(); }
void NativeAppsSurface::mousePressEvent(QMouseEvent *e) { forceActiveFocus(Qt::MouseFocusReason); if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=e->position().x(),.y=e->position().y(),.buttons=1}); refresh(); e->accept(); }
void NativeAppsSurface::mouseReleaseEvent(QMouseEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=e->position().x(),.y=e->position().y()}); refresh(); e->accept(); }
void NativeAppsSurface::mouseUngrabEvent() { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_cancel}); refresh(); }
void NativeAppsSurface::wheelEvent(QWheelEvent *e) { const bool precise=!e->pixelDelta().isNull(); const auto d=precise?e->pixelDelta():e->angleDelta(); const double scale=precise?1.0:0.125; if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::scroll,.x=e->position().x(),.y=e->position().y(),.scroll_y=-double(d.y())*scale,.scroll_x=-double(d.x())*scale,.precise_scroll=precise}); refresh(); e->accept(); }
void NativeAppsSurface::keyPressEvent(QKeyEvent *e) {
    if (!m->view) { e->ignore(); return; }
    if (e->matches(QKeySequence::Paste)) { const auto text=QGuiApplication::clipboard()->text(); if (!text.isEmpty()) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=text.toStdString()}); refresh(); e->accept(); return; }
    using K=ii::gui::WidgetKey; auto k=key(e->key());
    if (e->matches(QKeySequence::SelectAll)) k=K::select_all; else if (e->matches(QKeySequence::Copy)) k=K::copy;
    else if (e->matches(QKeySequence::Cut)) k=K::cut; else if (e->matches(QKeySequence::Undo)) k=K::undo; else if (e->matches(QKeySequence::Redo)) k=K::redo;
    if (k!=K::none) { (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_down,.key=k,.shift=bool(e->modifiers()&Qt::ShiftModifier),.control=bool(e->modifiers()&Qt::ControlModifier),.meta=bool(e->modifiers()&Qt::MetaModifier)}); if (k==K::space && !(e->modifiers()&(Qt::ControlModifier|Qt::MetaModifier))) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=" "}); }
    else if (!e->text().isEmpty() && !(e->modifiers()&(Qt::ControlModifier|Qt::MetaModifier))) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=e->text().toStdString()});
    else { e->ignore(); return; } refresh(); e->accept();
}
void NativeAppsSurface::keyReleaseEvent(QKeyEvent *e) { const auto k=key(e->key()); if (m->view && k!=ii::gui::WidgetKey::none) (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_up,.key=k}); refresh(); e->accept(); }
void NativeAppsSurface::inputMethodEvent(QInputMethodEvent *e) { if (m->view) { if (!e->commitString().isEmpty()) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=e->commitString().toStdString()}); (void)m->view->input({.kind=ii::gui::WidgetInputKind::composition,.text=e->preeditString().toStdString()}); } refresh(); e->accept(); }
QVariant NativeAppsSurface::inputMethodQuery(Qt::InputMethodQuery query) const { if (query==Qt::ImEnabled) return true; return QQuickPaintedItem::inputMethodQuery(query); }
void NativeAppsSurface::focusOutEvent(QFocusEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::focus_lost}); refresh(); QQuickPaintedItem::focusOutEvent(e); }
