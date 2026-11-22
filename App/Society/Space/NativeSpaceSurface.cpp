#include "NativeSpaceSurface.h"
#include "NativeSpaceViewModel.h"
#include "SpaceOrbGaze.h"
#include "SpaceDictation.h"
#include "NativeSpace.generated.h"
#include "Society/Native/QtPaintDevice.h"
#include "Society/Native/ExternalOverlaySurface.h"
#include "Society/Files/ModelBackendClient.h"
#include <iiGUI>
#include <iiGUI/PaintView.h>
#include <LVUI/StyleTokens.h>
#include <QClipboard>
#include <QCursor>
#include <QFile>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <locale>
#include <sstream>
#include <mutex>
#include <optional>

static void initializeSpaceAssets() { Q_INIT_RESOURCE(SpaceAssets); }
namespace {
using View = lvui_generated::NativeSpace;
/// Rasterize the source vector at three times the LVUI image's logical size.
constexpr std::uint32_t orbRasterSize = 504;
/// Polling also follows the pointer beyond the application window when supported.
constexpr int gazeSampleIntervalMs = 33;
/// Translate only keys supported by iiGUI; printable text is forwarded separately.
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
/// Preserve Unicode paths on both Windows and UTF-8 native filesystems.
std::filesystem::path nativePath(const QString &path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}
/// Generate an opaque identity; it does not encode a title or filesystem path.
std::string newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
}

struct NativeSpaceSurface::Impl final {
    std::shared_ptr<QtPaintDevice> device = std::make_shared<QtPaintDevice>();
    std::shared_ptr<ii::gui::PaintView> view;
    std::shared_ptr<View::Scope> scope;
    std::optional<iiSocietyContainer::SpaceStore> store;
    std::optional<SpaceOrbGaze> gaze;
    std::optional<QPoint> gazePixels = QPoint{};
    GlobalPointerProvider pointerProvider = [] { return std::optional<QPoint>(QCursor::pos()); };
    QTimer *gazeTimer = nullptr;
    QMetaObject::Connection windowVisibility;
    std::unique_ptr<ModelBackendClient> agent;
    std::shared_ptr<society::native::ExternalOverlaySurface> picker;
    std::unique_ptr<SpaceDictation> dictation;
    std::string dictationBase, dictationRendered;
    quint64 dictationGeneration=0;
    QString root, error;
    std::string connectionGeometry;
    bool initialized = false;
    QImage image;
    std::mutex mutex;
};

NativeSpaceSurface::NativeSpaceSurface(QQuickItem *parent) : QQuickPaintedItem(parent), m(std::make_unique<Impl>()) {
    setAcceptHoverEvents(true); setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusScope, true); setFlag(ItemAcceptsInputMethod, true); setActiveFocusOnTab(true);
    initializeSpaceAssets();
    QFile orb(QStringLiteral(":/society/space/minimal-cyan-orb.svg"));
    if (!orb.open(QIODevice::ReadOnly)) { m->error = tr("Could not load the Space AI image."); return; }
    auto gaze = SpaceOrbGaze::create(orb.readAll(), orbRasterSize);
    if (!gaze) { m->error = gaze.error(); return; }
    m->gaze = std::move(*gaze);
    auto decoded = m->gaze->imageAt({});
    if (!decoded) { m->error = decoded.error(); return; }
    const QPointer<NativeSpaceSurface> guard(this);
    auto result = ii::gui::PaintView::create(m->device, [&](ii::gui::WidgetBuilder &builder) {
        return View::build(builder, {}, [&](View::Scope &scope) {
            auto &vm = scope.backend_0;
            vm.orbImage.set(*decoded);
            vm.onSelectSession = [guard](const std::string &id) {
                if (guard) guard->selectSession(id);
            };
            vm.onCreateSession = [guard](const std::string &title) {
                if (!guard || !guard->m->store || !guard->model()) return;
                const auto id = newId();
                auto saved = guard->m->store->createSession({id, title, "", "active"});
                if (!saved) { guard->model()->status.set(saved.error()); return; }
                guard->model()->sessionEdited(""); guard->model()->creatingSession.set(false);
                guard->selectSession(id);
            };
            vm.onAddIdea = [guard](const std::string &title) {
                if (!guard || !guard->m->store || !guard->model()) return;
                const auto id = newId(), session = guard->model()->currentSessionId.get();
                if (session.empty()) return;
                auto saved = guard->m->store->createIdea({id, session, title, "", "User", "", 1});
                if (!saved) { guard->model()->status.set(saved.error()); return; }
                guard->model()->ideaEdited("");
                if (guard->selectSession(session)) guard->model()->selectIdea(id);
            };
            vm.onAttach = [guard] { if (guard) guard->chooseAttachments(); };
            vm.onMicrophone = [guard] { if (guard) guard->toggleDictation(); };
            vm.onSend = [guard](const std::string &prompt,const std::vector<std::string> &files,const std::string &effort) {
                if (!guard || !guard->m->agent) return false;
                QJsonArray attachments; for (const auto &path:files) attachments.append(QString::fromStdString(path));
                return guard->m->agent->request(QStringLiteral("spaceSend"),
                    {{"prompt",QString::fromStdString(prompt)},{"attachments",attachments},{"effort",QString::fromStdString(effort)}},true);
            };
        }, &m->scope);
    }, View::appearance());
    if (!result) { m->error = QString::fromStdString(result.error().message); return; }
    m->view = *result; m->initialized = true;
    m->dictation=makeSpaceDictation(this);
    m->agent=std::make_unique<ModelBackendClient>();
    connect(m->agent.get(),&ModelBackendClient::spaceStateReceived,this,[this](const QString &session,const QJsonObject &state) {
        auto *vm=model(); if (!vm || session.toStdString()!=vm->currentSessionId.get()) return;
        std::vector<NativeSpaceLine> entries;
        for (const auto &value:state.value("messages").toArray()) {
            const auto message=value.toObject();
            entries.push_back({message.value("id").toString().toStdString(),message.value("role").toString().toStdString(),message.value("text").toString().toStdString()});
        }
        vm->applyAgentState(std::move(entries),state.value("status").toString().toStdString(),
            state.value("available").toBool(),state.value("busy").toBool(),state.value("retryable").toBool());
        refresh();
    });
    connect(m->agent.get(),&ModelBackendClient::spaceResultReceived,this,[this](const QString &,const QString &session,const QString &,bool accepted,const QString &error) {
        if (!accepted && model() && session.toStdString()==model()->currentSessionId.get()) {
            model()->agentStatus.set((error.isEmpty()?tr("The Space request could not be started."):error).toStdString());
            refresh();
        }
    });
    connect(m->agent.get(),&ModelBackendClient::connectedChanged,this,[this] {
        if (!m->agent->connected() && model() && model()->hasSession.get()) {
            model()->setAgentAvailable(false); model()->agentStatus.set("Connecting to the Space agent…"); refresh();
        }
    });
    connect(m->agent.get(),&ModelBackendClient::requestError,this,[this](const QString &error) {
        if (model()) { model()->agentStatus.set(error.toStdString()); refresh(); }
    });
    m->gazeTimer = new QTimer(this);
    m->gazeTimer->setObjectName(QStringLiteral("spaceGazeTimer"));
    m->gazeTimer->setInterval(gazeSampleIntervalMs);
    connect(m->gazeTimer, &QTimer::timeout, this, &NativeSpaceSurface::sampleGazePointer);
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (!isVisible()) { cancelDictation(); if (m->picker) m->picker->requestClose(); }
        updateGazeTracking();
    });
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *host) {
        disconnect(m->windowVisibility);
        if (host) m->windowVisibility = connect(host, &QWindow::visibilityChanged, this, [this] { updateGazeTracking(); });
        refresh();
        updateGazeTracking();
    });
    if (window()) m->windowVisibility = connect(window(), &QWindow::visibilityChanged, this, [this] { updateGazeTracking(); });
    updateGazeTracking();
}
NativeSpaceSurface::~NativeSpaceSurface() {
    cancelDictation();
    if (m->picker) m->picker->requestClose();
    if (m->agent) m->agent->stop();
    if (m->gazeTimer) m->gazeTimer->stop();
    disconnect(m->windowVisibility);
    disconnect(this, nullptr, this, nullptr);
    if (m->view) (void)m->view->close();
}
bool NativeSpaceSurface::ready() const { return m->initialized && bool(m->view); }
QString NativeSpaceSurface::errorString() const { return m->error; }
NativeSpaceViewModel *NativeSpaceSurface::model() const { return m->scope ? &m->scope->backend_0 : nullptr; }
void NativeSpaceSurface::setGlobalPointerProvider(GlobalPointerProvider provider) {
    m->pointerProvider = provider ? std::move(provider) : GlobalPointerProvider([] { return std::optional<QPoint>(QCursor::pos()); });
    if (m->gazeTimer && m->gazeTimer->isActive()) sampleGazePointer();
}
void NativeSpaceSurface::setDictationProvider(std::unique_ptr<SpaceDictation> provider) {
    cancelDictation(); m->dictation=std::move(provider);
}
void NativeSpaceSurface::cancelDictation() {
    ++m->dictationGeneration;
    if (m->dictation) m->dictation->cancel();
    if (model()) model()->recording.set(false);
}
void NativeSpaceSurface::toggleDictation() {
    auto *vm=model();
    if (!vm || !vm->hasSession.get() || !m->dictation) return;
    if (vm->recording.get()) {
        vm->recording.set(false); vm->agentStatus.set("Finishing speech recognition…"); m->dictation->stop(); refresh(); return;
    }
    const auto session=vm->currentSessionId.get();
    const auto generation=++m->dictationGeneration;
    m->dictationBase=vm->promptDraft.get(); m->dictationRendered=m->dictationBase;
    vm->recording.set(true); vm->microphoneStatus.set("Starting microphone…"); vm->agentStatus.set("Starting microphone…");
    const QPointer<NativeSpaceSurface> guard(this);
    m->dictation->start([guard,session,generation](std::string text,bool final,std::string error) {
        if (!guard || !guard->model() || guard->m->dictationGeneration!=generation || guard->model()->currentSessionId.get()!=session) return;
        auto *current=guard->model();
        if (!final && error.empty()) current->microphoneStatus.set("Listening…");
        if (!text.empty()) {
            if (current->promptDraft.get()!=guard->m->dictationRendered) {
                guard->cancelDictation(); current->agentStatus.set("Dictation stopped because the draft was edited."); guard->refresh(); return;
            }
            auto next=guard->m->dictationBase;
            if (!next.empty() && !std::isspace(static_cast<unsigned char>(next.back()))) next+=' ';
            next+=text; current->promptEdited(next); guard->m->dictationRendered=std::move(next);
        }
        if (final) {
            current->recording.set(false);
            current->agentStatus.set(!error.empty() ? error : guard->m->dictationRendered==guard->m->dictationBase
                ? "No speech was recognized." : "Speech added to the draft.");
        }
        guard->refresh();
    });
    refresh();
}
void NativeSpaceSurface::updateGazeTracking() {
    if (!m->gazeTimer) return;
    if (isVisible() && window() && window()->isVisible() && window()->visibility() != QWindow::Minimized) {
        if (!m->gazeTimer->isActive()) m->gazeTimer->start();
        sampleGazePointer();
    } else {
        m->gazeTimer->stop();
        if (model() && model()->recording.get()) cancelDictation();
    }
}
void NativeSpaceSurface::sampleGazePointer() {
    if (!window()) return;
    const auto global = m->pointerProvider();
    if (!global) return;
    const QPointF local = mapFromScene(window()->mapFromGlobal(*global));
    if (updateGaze(local)) refresh();
}
bool NativeSpaceSurface::updateGaze(const QPointF &pointer) {
    if (!m->gaze || !model()) return false;
    const auto bounds = controlBounds(QStringLiteral("space-orb"));
    if (bounds.isEmpty()) return false;
    const auto offset = SpaceOrbGaze::offsetForPointer(pointer, bounds);
    const auto pixels = SpaceOrbGaze::rasterOffset(offset, bounds, QSize(orbRasterSize, orbRasterSize));
    if (m->gazePixels && *m->gazePixels == pixels) return false;
    auto image = m->gaze->imageAt(pixels);
    if (!image) { m->error = image.error(); return false; }
    model()->orbImage.set(*image);
    m->gazePixels = pixels;
    return true;
}
bool NativeSpaceSurface::openContainer(const QString &root) {
    if (!model()) return false;
    if (root == m->root && m->store) return reload();
    cancelDictation(); if (m->picker) m->picker->requestClose();
    if (m->agent) { m->agent->setSpaceSession({}); m->agent->setContainerPath({}); }
    m->store.reset(); m->root.clear();
    model()->clear("Opening Space…");
    auto opened = iiSocietyContainer::SpaceStore::open(nativePath(root));
    if (!opened) {
        m->error = QString::fromStdString(opened.error()); model()->status.set(opened.error()); refresh(); return false;
    }
    m->store = std::move(*opened); m->root = root;
    if (m->agent) { m->agent->setContainerPath(root); m->agent->start(); }
    model()->storeAvailable.set(true);
    return reload();
}
void NativeSpaceSurface::clearContainer() {
    cancelDictation();
    if (m->picker) m->picker->requestClose();
    if (m->agent) { m->agent->setSpaceSession({}); m->agent->setContainerPath({}); m->agent->stop(); }
    m->store.reset(); m->root.clear(); m->error.clear();
    if (model()) model()->clear("Connect a Society container to open Space.");
    refresh();
}
bool NativeSpaceSurface::reload() {
    if (!m->store || !model()) return false;
    auto sessions = m->store->sessions();
    if (!sessions) { model()->clear(sessions.error()); m->error = QString::fromStdString(sessions.error()); refresh(); return false; }
    model()->storeAvailable.set(true);
    auto id = model()->currentSessionId.get();
    if (std::none_of(sessions->begin(), sessions->end(), [&](const auto &s) { return s.id == id; }))
        id = sessions->empty() ? std::string{} : sessions->front().id;
    if (id.empty()) {
        if (m->agent) m->agent->setSpaceSession({});
        model()->setSnapshot(*sessions, std::nullopt); model()->status.set("No sessions yet.");
        m->error.clear(); refresh(); return true;
    }
    const bool loaded = selectSession(id);
    refresh();
    return loaded;
}
bool NativeSpaceSurface::selectSession(const std::string &id) {
    if (!m->store || !model()) return false;
    auto sessions = m->store->sessions();
    if (!sessions) { model()->clear(sessions.error()); m->error = QString::fromStdString(sessions.error()); return false; }
    auto graph = m->store->graph(id);
    if (!graph) { model()->clear(graph.error()); m->error = QString::fromStdString(graph.error()); return false; }
    if (model()->currentSessionId.get()!=id) {
        cancelDictation(); if (m->picker) m->picker->requestClose();
    }
    model()->setSnapshot(*sessions, std::move(*graph));
    if (m->agent) m->agent->setSpaceSession(QString::fromStdString(id));
    model()->status.set("Saved in the current Society container."); m->error.clear(); return true;
}
void NativeSpaceSurface::chooseAttachments() {
    if (!model() || !model()->hasSession.get() || !window() || m->picker) return;
    const QPointer<NativeSpaceSurface> guard(this);
    const auto session=model()->currentSessionId.get();
    auto wanted=[guard,session] { return guard && guard->isVisible() && guard->model() && guard->model()->currentSessionId.get()==session; };
    m->picker=std::make_shared<society::native::ExternalOverlaySurface>(window(),wanted,
        [guard,session](ii::gui::Host &host) {
            ii::gui::FileDialogOptions options{.mode=ii::gui::FileDialogMode::open,.title="Add files and photos",.multiple=true};
            const auto started=host.choose_file(std::move(options),[guard,session](ii::core::Result<ii::gui::FileDialogSelection> selection) {
                if (!guard) return;
                if (guard->model() && guard->model()->currentSessionId.get()==session) {
                    if (!selection) guard->model()->agentStatus.set("Could not open the file chooser.");
                    else if (!selection->canceled) guard->model()->addAttachments(selection->paths);
                    guard->refresh();
                }
                if (guard->m->picker) guard->m->picker->requestClose();
            });
            return bool(started);
        }, [] {});
    auto picker=m->picker;
    if (!picker->start([guard](bool failed) {
        if (!guard) return;
        guard->m->picker.reset();
        if (failed && guard->model()) { guard->model()->agentStatus.set("Could not open the file chooser."); guard->refresh(); }
    })) {
        m->picker.reset(); model()->agentStatus.set("Could not open the file chooser."); refresh();
    }
}
QRectF NativeSpaceSurface::controlBounds(const QString &id) const {
    if (!m->view) return {};
    auto item = m->view->find(id.toStdString()); if (!item) return {};
    auto snapshot = item->snapshot(); if (!snapshot) return {};
    const auto &b = snapshot->bounds; return {b.x, b.y, b.width, b.height};
}
QRectF NativeSpaceSurface::keyedControlBounds(const QString &host, const QString &key, const QString &id) const {
    if (!m->view) return {};
    const auto collection = m->view->find(host.toStdString()); if (!collection) return {};
    const auto item = collection->keyed_content_item(key.toStdString(), id.toStdString()); if (!item) return {};
    const auto snapshot = item->snapshot(); if (!snapshot) return {};
    const auto &b = snapshot->bounds; return {b.x, b.y, b.width, b.height};
}
bool NativeSpaceSurface::controlEnabled(const QString &id) const {
    if (!m->view) return false;
    const auto item = m->view->find(id.toStdString()); if (!item) return false;
    const auto snapshot = item->snapshot(); return snapshot && snapshot->enabled;
}
bool NativeSpaceSurface::activateControl(const QString &id) {
    if (!m->view) return false;
    auto item = m->view->find(id.toStdString());
    if (!item || !item->activate()) return false;
    refresh(); return true;
}
bool NativeSpaceSurface::updateConnections(double scale) {
    auto *vm = model();
    if (!vm || !m->view) return false;
    const auto canvas = m->view->find("space-graph");
    if (!canvas) return false;
    const auto snapshot = canvas->snapshot();
    if (!snapshot) return false;
    const auto &b = snapshot->bounds;
    if (b.width <= 0 || b.height <= 0) return false;
    const QRectF canvasBounds(b.x, b.y, b.width, b.height);
    auto centerFor = [&](const std::string &id) -> std::optional<QPointF> {
        for (const auto &[hostId, viewportId] : {std::pair{"space-left-nodes", "space-left-ideas"},
                                                  std::pair{"space-right-nodes", "space-right-ideas"}}) {
            const auto host = m->view->find(hostId), viewport = m->view->find(viewportId);
            if (!host || !viewport) continue;
            const auto item = host->keyed_content_item(id);
            if (!item) continue;
            const auto node = item->snapshot(), clip = viewport->snapshot();
            if (!node || !clip) continue;
            const auto &n = node->bounds; const auto &c = clip->bounds;
            const QPointF center(n.x + n.width / 2, n.y + n.height / 2);
            if (!QRectF(c.x, c.y, c.width, c.height).contains(center) || !canvasBounds.contains(center)) continue;
            return center - canvasBounds.topLeft();
        }
        return std::nullopt;
    };
    std::ostringstream lines;
    lines.imbue(std::locale::classic());
    for (const auto &edge : vm->connections.get()) {
        const auto from = centerFor(edge.fromId), to = centerFor(edge.toId);
        if (!from || !to) continue;
        lines << "<line x1=\"" << from->x() << "\" y1=\"" << from->y()
              << "\" x2=\"" << to->x() << "\" y2=\"" << to->y() << "\"/>";
    }
    const auto segments = lines.str();
    std::ostringstream identity;
    identity.imbue(std::locale::classic());
    identity << b.width << ':' << b.height << ':' << scale << ':' << segments;
    const auto geometry = identity.str();
    if (geometry == m->connectionGeometry) return false;
    m->connectionGeometry = geometry;
    if (segments.empty()) { vm->connectionsImage.set({}); return true; }
    // Color is read from LVUI's authored text token, not an application palette.
    const auto color = lvui::figma_defaults::text_body_color;
    std::ostringstream svg;
    svg.imbue(std::locale::classic());
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << b.width << ' ' << b.height
        << "\"><g fill=\"none\" stroke=\"rgb(" << int(color.red * 255) << ',' << int(color.green * 255)
        << ',' << int(color.blue * 255) << ")\" stroke-opacity=\"" << color.alpha
        << "\" stroke-width=\"1.5\" stroke-dasharray=\"7 7\">" << segments << "</g></svg>";
    const auto density = std::clamp(scale, 1.0, 2.0);
    const auto width = std::uint32_t(std::clamp(std::ceil(double(b.width) * density), 1.0, 8192.0));
    const auto height = std::uint32_t(std::clamp(std::ceil(double(b.height) * density), 1.0, 8192.0));
    if (std::uint64_t(width) * height > ii::gui::maximum_image_pixels) { vm->connectionsImage.set({}); return true; }
    auto image = ii::gui::svg_image(svg.str(), width, height);
    if (!image) { m->error = QString::fromStdString(image.error().message); vm->connectionsImage.set({}); return true; }
    vm->connectionsImage.set(std::move(*image));
    return true;
}
void NativeSpaceSurface::refresh() {
    if (!m->view || width() <= 0 || height() <= 0) return;
    if (auto *vm = model()) vm->resize(width(), height());
    const auto scale = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    auto rendered = m->view->paint({width(), height()}, scale);
    if (!rendered) { m->error = QString::fromStdString(rendered.error().message); return; }
    if (updateConnections(scale)) {
        rendered = m->view->paint({width(), height()}, scale);
        if (!rendered) { m->error = QString::fromStdString(rendered.error().message); return; }
    }
    if (m->scope) {
        const auto failures = m->scope->binding_failures();
        if (!failures) m->error = QString::fromStdString(failures.error().message);
        else if (!failures->empty()) m->error = QString::fromStdString(failures->front().failure.error.message);
    }
    { std::lock_guard lock(m->mutex); m->image = m->device->image(); }
    update();
}
void NativeSpaceSurface::paint(QPainter *painter) { std::lock_guard lock(m->mutex); if (!m->image.isNull()) painter->drawImage(QPointF(), m->image); }
void NativeSpaceSurface::geometryChange(const QRectF &next, const QRectF &old) { QQuickPaintedItem::geometryChange(next, old); if (next.size() != old.size()) refresh(); }
void NativeSpaceSurface::hoverMoveEvent(QHoverEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=e->position().x(),.y=e->position().y()}); (void)updateGaze(e->position()); refresh(); e->accept(); }
void NativeSpaceSurface::hoverLeaveEvent(QHoverEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_leave}); refresh(); e->accept(); }
void NativeSpaceSurface::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus(Qt::MouseFocusReason);
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=e->position().x(),.y=e->position().y(),
        .shift=bool(e->modifiers() & Qt::ShiftModifier),.control=bool(e->modifiers() & Qt::ControlModifier),
        .alt=bool(e->modifiers() & Qt::AltModifier),.meta=bool(e->modifiers() & Qt::MetaModifier),.buttons=1});
    refresh(); e->accept();
}
void NativeSpaceSurface::mouseReleaseEvent(QMouseEvent *e) {
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=e->position().x(),.y=e->position().y(),
        .shift=bool(e->modifiers() & Qt::ShiftModifier),.control=bool(e->modifiers() & Qt::ControlModifier),
        .alt=bool(e->modifiers() & Qt::AltModifier),.meta=bool(e->modifiers() & Qt::MetaModifier)});
    refresh(); e->accept();
}
void NativeSpaceSurface::mouseUngrabEvent() { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::pointer_cancel}); refresh(); }
void NativeSpaceSurface::wheelEvent(QWheelEvent *e) {
    const bool precise = !e->pixelDelta().isNull(); const auto delta = precise ? e->pixelDelta() : e->angleDelta();
    const double scale = precise ? 1.0 : 1.0 / 8.0;
    if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::scroll,.x=e->position().x(),.y=e->position().y(),
        .shift=bool(e->modifiers() & Qt::ShiftModifier),.scroll_y=-double(delta.y())*scale,.scroll_x=-double(delta.x())*scale,.precise_scroll=precise});
    refresh(); e->accept();
}
void NativeSpaceSurface::keyPressEvent(QKeyEvent *e) {
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
        if (k == K::space && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
            (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=" "});
    } else if (!e->text().isEmpty() && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
        (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=e->text().toStdString()});
    else { e->ignore(); return; }
    refresh(); e->accept();
}
void NativeSpaceSurface::keyReleaseEvent(QKeyEvent *e) { const auto k=key(e->key()); if (m->view && k!=ii::gui::WidgetKey::none) (void)m->view->input({.kind=ii::gui::WidgetInputKind::key_up,.key=k}); refresh(); e->accept(); }
void NativeSpaceSurface::inputMethodEvent(QInputMethodEvent *e) {
    if (m->view) {
        if (!e->commitString().isEmpty()) (void)m->view->input({.kind=ii::gui::WidgetInputKind::text,.text=e->commitString().toStdString()});
        (void)m->view->input({.kind=ii::gui::WidgetInputKind::composition,.text=e->preeditString().toStdString()});
    }
    refresh(); e->accept();
}
QVariant NativeSpaceSurface::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) return true;
    return QQuickPaintedItem::inputMethodQuery(query);
}
void NativeSpaceSurface::focusOutEvent(QFocusEvent *e) { if (m->view) (void)m->view->input({.kind=ii::gui::WidgetInputKind::focus_lost}); refresh(); QQuickPaintedItem::focusOutEvent(e); }
