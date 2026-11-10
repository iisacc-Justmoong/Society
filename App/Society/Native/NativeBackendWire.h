#pragma once
#include <iiJson/Json.h>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace society::native {
// One decoder per connection. A failed decoder is terminal until reset().
class BackendFrames final {
public:
    static constexpr std::size_t maximum_body = 1024 * 1024;
    [[nodiscard]] static std::optional<std::string> encode(const ii::core::Value &object);
    [[nodiscard]] bool feed(std::string_view bytes, std::vector<ii::core::Value> &messages);
    void reset() noexcept;
    [[nodiscard]] const std::string &error() const noexcept { return m_error; }
private:
    std::string m_pending, m_error;
    std::size_t m_expected = 0;
};
}
