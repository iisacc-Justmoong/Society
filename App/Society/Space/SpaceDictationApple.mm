#include "SpaceDictation.h"
#include <iiDeviceMultimedia/IO/Hardware.h>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <cstring>
#include <exception>
#include <optional>
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>

namespace {
/// All mutable state belongs to the Qt owner thread; native callbacks post to it.
struct RecognitionState final {
    QPointer<QObject> owner;
    std::uint64_t generation=0;
    SpaceDictation::Update update;
    std::unique_ptr<ii::device::Hardware> hardware;
    std::optional<ii::device::Endpoint> endpoint;
    QPointer<QTimer> timer;
    __strong SFSpeechRecognizer *recognizer=nil;
    __strong SFSpeechAudioBufferRecognitionRequest *request=nil;
    __strong SFSpeechRecognitionTask *task=nil;
    __strong AVAudioFormat *format=nil;
};

/// Queue authorization and recognition events on the application owner thread.
void post(std::function<void()> action) {
    if (auto *app=QCoreApplication::instance()) QMetaObject::invokeMethod(app,std::move(action),Qt::QueuedConnection);
}
/// Retire the iiSDK endpoint only after its host-driven poll has returned.
void closeAudio(const std::shared_ptr<RecognitionState> &state) {
    if (state->timer) { state->timer->stop(); state->timer->deleteLater(); state->timer=nullptr; }
    if (state->hardware) {
        if (state->endpoint) (void)state->hardware->close(*state->endpoint);
        (void)state->hardware->close(); state->hardware.reset(); state->endpoint.reset();
    }
}
/// Complete a turn once, leaving dictated text available to the editable field.
void finish(const std::shared_ptr<RecognitionState> &state,std::uint64_t generation,std::string text,std::string error) {
    if (!state->owner || state->generation!=generation || !state->update) return;
    closeAudio(state); auto update=std::move(state->update);
    if (!error.empty() && state->task) [state->task cancel];
    state->task=nil; state->request=nil; state->recognizer=nil; state->format=nil;
    update(std::move(text),true,std::move(error));
}

/// Start the local Speech recognizer and feed it mono PCM from iiDeviceMultimedia.
void startAudio(const std::shared_ptr<RecognitionState> &state,std::uint64_t generation) {
    if (!state->owner || state->generation!=generation || !state->update) return;
    state->recognizer=[[SFSpeechRecognizer alloc] initWithLocale:[NSLocale currentLocale]];
    if (!state->recognizer || !state->recognizer.available || !state->recognizer.supportsOnDeviceRecognition) {
        finish(state,generation,{},"On-device speech recognition is unavailable for the current language."); return;
    }
    state->request=[[SFSpeechAudioBufferRecognitionRequest alloc] init];
    state->request.requiresOnDeviceRecognition=YES;
    state->request.shouldReportPartialResults=YES;
    state->format=[[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:16000 channels:1 interleaved:NO];
    if (!state->format) { finish(state,generation,{},"Could not prepare microphone audio."); return; }
    const std::weak_ptr<RecognitionState> weak=state;
    state->task=[state->recognizer recognitionTaskWithRequest:state->request resultHandler:^(SFSpeechRecognitionResult *result,NSError *error) {
        const std::string text=result ? std::string(result.bestTranscription.formattedString.UTF8String ?: "") : std::string{};
        const std::string detail=error ? std::string(error.localizedDescription.UTF8String ?: "Speech recognition failed.") : std::string{};
        const bool final=result && result.isFinal;
        post([weak,generation,text,detail,final] {
            auto current=weak.lock();
            if (!current || !current->owner || current->generation!=generation || !current->update) return;
            if (final || !detail.empty()) finish(current,generation,text,detail);
            else if (!text.empty()) current->update(text,false,{});
        });
    }];
    if (!state->task) { finish(state,generation,{},"Could not start speech recognition."); return; }
    state->hardware=std::make_unique<ii::device::Hardware>([weak,generation](ii::device::HardwareEvent event) {
        auto current=weak.lock();
        if (!current || current->generation!=generation || !current->request) return;
        if (event.kind==ii::device::HardwareEventKind::failure || event.error) {
            // Poll owns the endpoint during this callback; retire it after poll returns.
            post([weak,generation] { if (auto state=weak.lock()) finish(state,generation,{},"Microphone capture failed."); });
            return;
        }
        if (event.samples.empty()) return;
        AVAudioPCMBuffer *buffer=[[AVAudioPCMBuffer alloc] initWithPCMFormat:current->format frameCapacity:AVAudioFrameCount(event.samples.size())];
        if (!buffer || !buffer.floatChannelData) return;
        buffer.frameLength=AVAudioFrameCount(event.samples.size());
        std::memcpy(buffer.floatChannelData[0],event.samples.data(),event.samples.size()*sizeof(float));
        [current->request appendAudioPCMBuffer:buffer];
    });
    auto endpoint=state->hardware->open_audio(ii::device::DeviceKind::audio_input,{},
        {.sample_rate=16000,.channels=1,.frames_per_buffer=512,.queued_frames=16384});
    if (!endpoint) { [state->task cancel]; finish(state,generation,{},"Could not open the microphone."); return; }
    state->endpoint=*endpoint;
    auto *timer=new QTimer(state->owner); state->timer=timer; timer->setInterval(20);
    QObject::connect(timer,&QTimer::timeout,state->owner,[weak,generation] {
        auto current=weak.lock(); if (!current || current->generation!=generation || !current->hardware) return;
        const auto polled=current->hardware->poll();
        if (!polled) finish(current,generation,{},"Microphone capture failed.");
    });
    timer->start();
    if (state->update) state->update({},false,{});
}

/// Requests microphone and speech permissions only after the user enables dictation.
class AppleDictation final : public SpaceDictation {
public:
    /// Bind all asynchronous recognition work to the owning UI object.
    explicit AppleDictation(QObject *owner) : state_(std::make_shared<RecognitionState>()) { state_->owner=owner; }
    /// Retire the endpoint and suppress late native callbacks before destruction.
    ~AppleDictation() override { cancel(); }
    /// Request authorization asynchronously and admit on-device recognition only.
    void start(Update update) override {
        cancel(); const auto generation=state_->generation; state_->update=std::move(update);
        const std::weak_ptr<RecognitionState> weak=state_;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            post([weak,generation,granted] {
                auto state=weak.lock(); if (!state || !state->owner || state->generation!=generation || !state->update) return;
                if (!granted) { finish(state,generation,{},"Microphone access was denied."); return; }
                [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
                    post([weak,generation,status] {
                        auto state=weak.lock(); if (!state || !state->owner || state->generation!=generation || !state->update) return;
                        if (status!=SFSpeechRecognizerAuthorizationStatusAuthorized) {
                            finish(state,generation,{},"Speech recognition access was denied."); return;
                        }
                        try { startAudio(state,generation); }
                        catch (const std::exception &error) { finish(state,generation,{},error.what()); }
                        catch (...) { finish(state,generation,{},"Could not start microphone input."); }
                    });
                }];
            });
        }];
    }
    /// Stop collecting samples and allow a bounded interval for the final transcript.
    void stop() override {
        closeAudio(state_);
        if (state_->request) {
            [state_->request endAudio];
            const auto generation=state_->generation; const std::weak_ptr<RecognitionState> weak=state_;
            if (state_->owner) QTimer::singleShot(5000,state_->owner,[weak,generation] {
                if (auto state=weak.lock(); state && state->generation==generation && state->update)
                    finish(state,generation,{},"Speech recognition did not finish. The draft was kept.");
            });
        }
        else if (state_->update) finish(state_,state_->generation,{},"Microphone input stopped.");
    }
    /// Close the endpoint and invalidate all outstanding authorization or result callbacks.
    void cancel() override {
        ++state_->generation; closeAudio(state_);
        if (state_->task) [state_->task cancel];
        state_->task=nil; state_->request=nil; state_->recognizer=nil; state_->format=nil; state_->update={};
    }
private:
    std::shared_ptr<RecognitionState> state_;
};
}
std::unique_ptr<SpaceDictation> makeSpaceDictation(QObject *owner) { return std::make_unique<AppleDictation>(owner); }
