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
        if (!url.isLocalFile() || QFileInfo(url.toLocalFile()).suffix().compare("societycontainer", Qt::CaseInsensitive) != 0)
            return false;
        m_pending = url; flush(); event->accept(); return true;
    }
private:
    QUrl m_pending;
    std::function<bool(const QUrl &)> m_opener;
    bool m_opening = false;
};
