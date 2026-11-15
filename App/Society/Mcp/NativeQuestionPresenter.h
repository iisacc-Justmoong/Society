#pragma once
namespace society::native { class OverlayShutdown; }
class QObject;
namespace iiMultimodalAgent::agent { class QuestionInbox; }

namespace society::questions {
// Installs the event-driven native question surface on a supported desktop
// host. The caller keeps the legacy transport available if admission fails.
[[nodiscard]] bool installNativeQuestionPresenter(QObject* root,
    iiMultimodalAgent::agent::QuestionInbox* inbox, QObject* lifetime,
    society::native::OverlayShutdown* shutdown);
}
