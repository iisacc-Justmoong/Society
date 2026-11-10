#include "NativeModelChooserState.h"

#include <stdexcept>
#include <utility>

namespace society {

ModelChooserState::ModelChooserState(std::vector<ModelChoice> choices,
                                     const std::vector<std::string> &selected)
    : m_choices(std::move(choices)), m_selected(m_choices.size(), false)
{
    if (m_choices.size() > 65536) throw std::invalid_argument("Too many models in the catalogue");
    m_positions.reserve(m_choices.size());
    for (std::size_t i = 0; i < m_choices.size(); ++i) {
        const auto &choice = m_choices[i];
        if (choice.path.empty() || choice.name.empty() || !m_positions.emplace(choice.path, i).second)
            throw std::invalid_argument("The model catalogue has an invalid or duplicate entry");
    }
    for (const auto &path : selected) {
        const auto found = m_positions.find(path);
        if (found == m_positions.end() || m_selected[found->second])
            throw std::invalid_argument("The selected model is not uniquely present in the catalogue");
        m_selected[found->second] = true;
        ++m_count;
    }
}

bool ModelChooserState::toggle(std::size_t index)
{
    if (index >= m_choices.size()) return false;
    m_selected[index] = !m_selected[index];
    if (m_selected[index]) ++m_count; else --m_count;
    return true;
}

bool ModelChooserState::isSelected(std::size_t index) const
{
    return index < m_selected.size() && m_selected[index];
}

std::vector<std::string> ModelChooserState::selectedPaths() const
{
    std::vector<std::string> result;
    result.reserve(m_count);
    for (std::size_t i = 0; i < m_choices.size(); ++i)
        if (m_selected[i]) result.push_back(m_choices[i].path);
    return result;
}

}
