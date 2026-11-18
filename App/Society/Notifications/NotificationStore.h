#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace society::notifications {

struct Notification final {
    std::string source;
    std::string message;
};

// Source state is deliberately independent of the visible list: dismissing a
// still-active warning must not produce a new notification on the next poll.
class NotificationStore final {
public:
    [[nodiscard]] const std::vector<Notification> &entries() const noexcept { return m_entries; }

    void record(std::string source, std::vector<std::string> lines) {
        const auto found = m_active.find(source);
        const auto wasActive = [&](const std::string &line) {
            return found != m_active.end() &&
                   std::find(found->second.begin(), found->second.end(), line) != found->second.end();
        };
        for (const auto &line : lines) {
            if (line.empty() || wasActive(line)) continue;
            const auto alreadyVisible = std::any_of(m_entries.begin(), m_entries.end(), [&](const auto &entry) {
                return entry.source == source && entry.message == line;
            });
            if (!alreadyVisible) m_entries.push_back({source, line});
        }
        m_active[std::move(source)] = std::move(lines);
    }

    bool dismiss(std::size_t index) {
        if (index >= m_entries.size()) return false;
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    void clear() { m_entries.clear(); }

private:
    std::vector<Notification> m_entries;
    std::unordered_map<std::string, std::vector<std::string>> m_active;
};
}
