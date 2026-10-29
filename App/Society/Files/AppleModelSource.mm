#include "AppleModelSource.h"

#import <Foundation/Foundation.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {
struct FileReadState {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool finished = false;
    bool abandoned = false;
    QString error;
    NSFileCoordinator *coordinator = nil;
};
}

ModelImportSource appleModelSource(NSURL *url)
{
    // Retain the original security-scoped URL from UIDocumentPicker; converting
    // it to a filesystem string first discards the provider's access grant.
    NSItemProvider *provider = [NSItemProvider new];
    provider.suggestedName = url.lastPathComponent;
    [provider registerFileRepresentationForTypeIdentifier:@"public.data"
        fileOptions:NSItemProviderFileOptionOpenInPlace visibility:NSItemProviderRepresentationVisibilityOwnProcess
        loadHandler:^NSProgress *(void (^completion)(NSURL *, BOOL, NSError *)) {
            completion(url, YES, nil);
            return nil;
        }];
    return appleModelSource(provider);
}

ModelImportSource appleModelSource(NSItemProvider *provider)
{
    return {QString::fromNSString(provider.suggestedName),
        [provider](const ModelImportSource::Consumer &consume,
                   const std::shared_ptr<std::atomic_bool> &cancelled) -> QString {
        @autoreleasepool {
            const auto state = std::make_shared<FileReadState>();
            // Keep the read callback inside NSFileCoordinator's access window. A provider
            // may delete its temporary representation as soon as that access ends.
            NSProgress *progress = [provider loadInPlaceFileRepresentationForTypeIdentifier:@"public.data"
                completionHandler:^(NSURL *url, BOOL, NSError *error) {
                {
                    std::lock_guard guard(state->mutex);
                    if (state->abandoned)
                        return;
                    state->started = true;
                }
                QString failure;
                @autoreleasepool {
                    if (!cancelled->load()) {
                        if (!url || error) {
                            failure = error ? QString::fromNSString(error.localizedDescription)
                                            : QStringLiteral("The file provider did not return a file.");
                        } else {
                            const BOOL scoped = [url startAccessingSecurityScopedResource];
                            NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
                            {
                                std::lock_guard guard(state->mutex);
                                state->coordinator = coordinator;
                            }
                            NSError *coordinationError = nil;
                            [coordinator coordinateReadingItemAtURL:url options:0 error:&coordinationError
                                byAccessor:^(NSURL *readableURL) {
                                if (!cancelled->load())
                                    consume(QString::fromNSString(readableURL.path));
                            }];
                            if (scoped)
                                [url stopAccessingSecurityScopedResource];
                            if (coordinationError)
                                failure = QString::fromNSString(coordinationError.localizedDescription);
                        }
                    }
                }
                {
                    std::lock_guard guard(state->mutex);
                    state->error = failure;
                    state->coordinator = nil;
                    state->finished = true;
                }
                state->changed.notify_one();
            }];
            std::unique_lock lock(state->mutex);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
            while (!state->finished) {
                if (cancelled->load() || (!state->started && std::chrono::steady_clock::now() >= deadline)) {
                    if (!state->started) {
                        // A late provider callback must never invoke the finished job.
                        state->abandoned = true;
                        lock.unlock();
                        [progress cancel];
                        return cancelled->load() ? QString()
                            : QStringLiteral("The file provider did not make the model available in time.");
                    }
                    // An active consumer observes the cancellation flag between chunks;
                    // wait for it to release coordinated access before returning.
                    NSFileCoordinator *coordinator = state->coordinator;
                    lock.unlock();
                    [coordinator cancel];
                    lock.lock();
                }
                state->changed.wait_for(lock, std::chrono::milliseconds(100));
            }
            return state->error;
        }
    }};
}
