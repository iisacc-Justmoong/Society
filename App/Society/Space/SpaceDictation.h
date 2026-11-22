#pragma once
#include <functional>
#include <memory>
#include <string>

class QObject;

/// Replaceable speech-input boundary. Callbacks run on the owning UI thread.
class SpaceDictation {
public:
    /// Text is the latest complete partial transcription; an empty nonfinal update
    /// announces capture readiness, and a final update or error ends the turn.
    using Update = std::function<void(std::string text, bool final, std::string error)>;
    virtual ~SpaceDictation() = default;
    /// Ask permission and start only in response to an explicit user action.
    virtual void start(Update update) = 0;
    /// Finish audio while allowing the recognizer to deliver its final text.
    virtual void stop() = 0;
    /// Immediately discard pending recognition and close the audio endpoint.
    virtual void cancel() = 0;
};

/// Provide the host's native recognizer, or an explicit unsupported implementation.
[[nodiscard]] std::unique_ptr<SpaceDictation> makeSpaceDictation(QObject *owner);
