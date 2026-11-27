#pragma once
#include <deque>
#include <string>
#include <string_view>
#include <utility>

namespace society::native {
// Holds error presentations independently of the persistent notification center.
class ErrorNotificationQueue final {
public:
    // Queue every nonempty occurrence in arrival order.
    void push(std::string message) { if (!message.empty()) messages_.push_back(std::move(message)); }
    // Report whether a presentation is waiting.
    [[nodiscard]] bool empty() const noexcept { return messages_.empty(); }
    // Read the first occurrence without changing the original record.
    [[nodiscard]] std::string_view current() const noexcept {
        return messages_.empty() ? std::string_view{} : std::string_view{messages_.front()};
    }
    // Remove the visible presentation and preserve the rest.
    void dismiss() noexcept { if (!messages_.empty()) messages_.pop_front(); }
private:
    std::deque<std::string> messages_;
};
}
