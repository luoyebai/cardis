#include <algorithm>
#include <stdexcept>

#include "cardis/core/character.hpp"

namespace cardis {
const CharacterDefinition& CharacterRoster::character(const std::string& id) const {
    const auto found =
        std::find_if(characters.begin(), characters.end(), [&id](const auto& value) { return value.id == id; });
    if (found == characters.end()) {
        throw std::invalid_argument("Unknown character: " + id);
    }
    return *found;
}

const GroupDefinition& CharacterRoster::group(const std::string& id) const {
    const auto found = std::find_if(groups.begin(), groups.end(), [&id](const auto& value) { return value.id == id; });
    if (found == groups.end()) {
        throw std::invalid_argument("Unknown character group: " + id);
    }
    return *found;
}
}  // namespace cardis
