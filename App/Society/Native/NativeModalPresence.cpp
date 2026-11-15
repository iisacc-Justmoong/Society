#include "NativeModalPresence.h"
#include <QThread>
#include <QVariant>
#include <QWindow>
#include <limits>
#include <stdexcept>

namespace society::native {
namespace { constexpr auto propertyName="_iisacc_native_modal_presence"; }
NativeModalPresence* NativeModalPresence::forWindow(QWindow* window) {
    if (!window || window->thread()!=QThread::currentThread()) return nullptr;
    const auto stored=window->property(propertyName);
    if (stored.isValid() && stored.metaType()!=QMetaType::fromType<QPointer<QObject>>()) return nullptr;
    const auto existing=stored.value<QPointer<QObject>>();
    if (existing) return dynamic_cast<NativeModalPresence*>(existing.data());
    auto* presence=new NativeModalPresence(window);
    window->setProperty(propertyName,QVariant::fromValue(QPointer<QObject>(presence)));
    if (window->property(propertyName).value<QPointer<QObject>>().data()!=presence) {delete presence;return nullptr;}
    return presence;
}
std::shared_ptr<NativeModalPresence::Lease> NativeModalPresence::acquire() {
    if (thread()!=QThread::currentThread()) throw std::logic_error("Native modal state requires the UI thread");
    const auto value=count_.get();
    if (value==std::numeric_limits<std::size_t>::max()) throw std::length_error("Native modal lease count exhausted");
    auto lease=std::shared_ptr<Lease>(new Lease(this));
    count_.set(value+1);
    return lease;
}
void NativeModalPresence::release() {
    if (thread()!=QThread::currentThread()) std::terminate();
    const auto value=count_.get();
    if (!value) std::terminate();
    count_.set(value-1);
}
NativeModalPresence::Lease::~Lease() {
    if (owner_) try {owner_->release();} catch (...) {}
}
} // namespace society::native
