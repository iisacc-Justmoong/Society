#include "NativeBackendWire.h"
#include <algorithm>
#include <cstdint>

namespace society::native {
std::optional<std::string> BackendFrames::encode(const ii::core::Value &object)
{
    if (object.kind() != ii::core::ValueKind::object) return std::nullopt;
    ii::json::Limits limits; limits.output_bytes = maximum_body;
    const auto body = ii::json::stringify(object, limits);
    if (!body || body->empty() || body->size() > maximum_body) return std::nullopt;
    const auto size = static_cast<std::uint32_t>(body->size());
    std::string frame(4, '\0');
    for (int i = 0; i < 4; ++i) frame[i] = static_cast<char>((size >> (24 - 8 * i)) & 0xffu);
    frame += *body; return frame;
}

bool BackendFrames::feed(std::string_view bytes, std::vector<ii::core::Value> &messages)
{
    if (!m_error.empty()) return false;
    while (!bytes.empty()) {
        if (!m_expected) {
            const auto n = std::min<std::size_t>(4 - m_pending.size(), bytes.size());
            m_pending.append(bytes.data(), n); bytes.remove_prefix(n);
            if (m_pending.size() < 4) continue;
            std::uint32_t size = 0;
            for (unsigned char c : m_pending) size = (size << 8) | c;
            m_pending.clear();
            if (!size || size > maximum_body) { m_error = "Invalid backend frame length."; return false; }
            m_expected = size;
        }
        const auto n = std::min(m_expected - m_pending.size(), bytes.size());
        m_pending.append(bytes.data(), n); bytes.remove_prefix(n);
        if (m_pending.size() != m_expected) continue;
        ii::json::Limits limits; limits.input_bytes = maximum_body;
        auto object = ii::json::parse(m_pending, limits);
        if (!object || object->kind() != ii::core::ValueKind::object) {
            m_error = "Invalid backend JSON object."; return false;
        }
        messages.push_back(std::move(*object)); m_pending.clear(); m_expected = 0;
    }
    return true;
}
void BackendFrames::reset() noexcept { m_pending.clear(); m_error.clear(); m_expected = 0; }
}
