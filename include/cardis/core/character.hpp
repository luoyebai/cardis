#pragma once

#include <array>
#include <string>
#include <vector>

namespace cardis {

enum class Gender { FEMALE, MALE, NON_BINARY, UNSPECIFIED };

struct GroupDefinition {
    std::string id;
    std::string name;
    std::string category;
};

// Fictional biography and Cardis gameplay settings are intentionally separate fields.
struct CharacterDefinition {
    std::string id;
    std::string name;
    std::string original_name;
    std::string title;
    std::string group_id;
    Gender gender = Gender::UNSPECIFIED;
    std::string species;
    std::string origin;
    std::string occupation;
    std::string biography;
    std::string source_url;
    std::string portrait;
    std::string artist;
    std::string license;
    std::vector<std::string> tags;
    std::string combat_role;
    int starting_life = 30;
    int max_mana = 10;
    std::vector<std::string> skill_ids;
};

struct CharacterRoster {
    std::array<std::string, 2> player_character_ids;
    std::vector<GroupDefinition> groups;
    std::vector<CharacterDefinition> characters;
    [[nodiscard]] const CharacterDefinition& character(const std::string& id) const;
    [[nodiscard]] const GroupDefinition& group(const std::string& id) const;
};

}  // namespace cardis
