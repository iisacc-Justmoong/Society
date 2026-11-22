#include "SpaceDictation.h"

namespace {
/// No audio is captured when a platform has no configured native recognizer.
class UnsupportedDictation final : public SpaceDictation {
public:
    void start(Update update) override { if (update) update({},true,"On-device speech recognition is not available on this platform."); }
    void stop() override {}
    void cancel() override {}
};
}
std::unique_ptr<SpaceDictation> makeSpaceDictation(QObject *) { return std::make_unique<UnsupportedDictation>(); }
