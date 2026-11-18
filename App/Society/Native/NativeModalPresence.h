#pragma once
#include <iiSDKCore>
#include <QObject>
#include <QPointer>
#include <cstddef>
#include <memory>

class QWindow;
namespace society::native {
// A window-local, event-driven count of native modal surfaces. Leases and the
// window owner are used and released on the UI thread.
class NativeModalPresence final : public QObject {
public:
    class Lease final {
    public:
        ~Lease();
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
    private:
        friend class NativeModalPresence;
        explicit Lease(NativeModalPresence* owner):owner_(owner) {}
        QPointer<NativeModalPresence> owner_;
    };
    explicit NativeModalPresence(QObject* parent=nullptr):QObject(parent) {}
    [[nodiscard]] static NativeModalPresence* forWindow(QWindow*);
    [[nodiscard]] std::shared_ptr<Lease> acquire();
    [[nodiscard]] ii::core::Observable<std::size_t>& count() noexcept {return count_;}
private:
    void release();
    ii::core::Observable<std::size_t> count_{0};
};
} // namespace society::native
