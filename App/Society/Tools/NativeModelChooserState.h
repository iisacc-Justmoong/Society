#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace society {

struct ModelChoice final {
    std::string path;
    std::string name;
    std::string category;
};

// Selection is independent of the storage catalogue and never changes files.
class ModelChooserState final {
public:
    explicit ModelChooserState(std::vector<ModelChoice> choices = {},
                               const std::vector<std::string> &selected = {});
    [[nodiscard]] bool toggle(std::size_t index);
    [[nodiscard]] bool isSelected(std::size_t index) const;
    [[nodiscard]] std::size_t selectedCount() const { return m_count; }
    [[nodiscard]] const std::vector<ModelChoice> &choices() const { return m_choices; }
    [[nodiscard]] std::vector<std::string> selectedPaths() const;
private:
    std::vector<ModelChoice> m_choices;
    std::vector<bool> m_selected;
    std::unordered_map<std::string, std::size_t> m_positions;
    std::size_t m_count = 0;
};

}
