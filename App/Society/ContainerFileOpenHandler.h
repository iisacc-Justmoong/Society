#pragma once
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QObject>
#include <QUrl>
#include <functional>

// Native file-open events can arrive before the QML drive controller exists.
class ContainerFileOpenHandler final : public QObject {
public:
    using QObject::QObject;
    void setOpener(std::function<bool(const QUrl &)> opener) {
        m_opener = std::move(opener); flush();
    }
    void setAccountOpener(std::function<void()> opener) {
        m_accountOpener = std::move(opener);
        if (m_accountPending && m_accountOpener) { m_accountPending = false; m_accountOpener(); }
    }
    void flush() {
        if (m_pending.isEmpty() || !m_opener || m_opening) return;
        const auto pending = m_pending;
        m_opening = true;
        if (m_opener(pending) && m_pending == pending) m_pending.clear();
        m_opening = false;
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::FileOpen) return QObject::eventFilter(object, event);
        const auto *opened = static_cast<QFileOpenEvent *>(event);
        const auto url = opened->url().isEmpty() ? QUrl::fromLocalFile(opened->file()) : opened->url();
        if (url.scheme() == "society" && url.host() == "account" && url.path().isEmpty()
            && url.query().isEmpty() && url.fragment().isEmpty() && url.userInfo().isEmpty()) {
            if (m_accountOpener) m_accountOpener(); else m_accountPending = true;
            event->accept(); return true;
        }
        if (!url.isLocalFile() || QFileInfo(url.toLocalFile()).suffix().compare("societycontainer", Qt::CaseInsensitive) != 0)
            return false;
        m_pending = url; flush(); event->accept(); return true;
    }
private:
    QUrl m_pending;
    std::function<bool(const QUrl &)> m_opener;
    bool m_opening = false, m_accountPending = false;
    std::function<void()> m_accountOpener;
};
