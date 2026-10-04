#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "cardis/content/catalog.hpp"

namespace cardis {
namespace {
Gender ParseGender(const std::string& gender) {
    if (gender == "female") {
        return Gender::FEMALE;
    }
    if (gender == "male") {
        return Gender::MALE;
    }
    if (gender == "non_binary") {
        return Gender::NON_BINARY;
    }
    if (gender == "unspecified") {
        return Gender::UNSPECIFIED;
    }
    throw std::invalid_argument("Unknown gender: " + gender);
}

int BoundedInteger(const nlohmann::json& value, int minimum, int maximum) {
    if (!value.is_number_integer() || value < minimum || value > maximum) {
        throw std::invalid_argument("Character stat outside supported integer range");
    }
    return value.get<int>();
}

void ValidatePortrait(const std::string& value) {
    const std::filesystem::path path(value);
    if (value.empty() || path.is_absolute() || path.has_root_name()) {
        throw std::invalid_argument("Character portrait must be a relative asset path");
    }
    for (const auto& part : path) {
        if (part == "..") {
            throw std::invalid_argument("Character portrait escapes asset directory");
        }
    }
}

CharacterDefinition ParseCharacter(const nlohmann::json& entry) {
    CharacterDefinition character;
    character.id = entry.at("id").get<std::string>();
    character.name = entry.at("name").get<std::string>();
    character.original_name = entry.at("original_name").get<std::string>();
    character.title = entry.at("title").get<std::string>();
    character.group_id = entry.at("group_id").get<std::string>();
    character.gender = ParseGender(entry.at("gender").get<std::string>());
    character.species = entry.at("species").get<std::string>();
    character.origin = entry.at("origin").get<std::string>();
    character.occupation = entry.at("occupation").get<std::string>();
    character.biography = entry.at("biography").get<std::string>();
    character.source_url = entry.at("source_url").get<std::string>();
    character.portrait = entry.at("portrait").get<std::string>();
    character.artist = entry.at("artist").get<std::string>();
    character.license = entry.at("license").get<std::string>();
    character.tags = entry.at("tags").get<std::vector<std::string>>();
    const auto& gameplay = entry.at("gameplay");
    character.combat_role = gameplay.at("role").get<std::string>();
    character.starting_life = BoundedInteger(gameplay.at("starting_life"), 1, 100);
    character.max_mana = BoundedInteger(gameplay.at("max_mana"), 1, 10);
    character.skill_ids = gameplay.at("skill_ids").get<std::vector<std::string>>();
    ValidatePortrait(character.portrait);
    if (character.id.empty() || character.name.empty() || character.combat_role.empty() ||
        character.skill_ids.empty() || character.skill_ids.size() > 64) {
        throw std::invalid_argument("Missing character identity or skills");
    }
    return character;
}
}  // namespace

CharacterRoster LoadRoster(const std::filesystem::path& manifest, const std::vector<CardDefinition>& cards) {
    std::ifstream input(manifest);
    if (!input) {
        throw std::runtime_error("Cannot open character manifest: " + manifest.string());
    }
    const auto root = nlohmann::json::parse(input);
    if (root.at("schema_version") != 1 || !root.at("groups").is_array() || !root.at("characters").is_array() ||
        !root.at("players").is_array() || root.at("players").size() != 2) {
        throw std::invalid_argument("Unsupported character manifest schema");
    }
    CharacterRoster roster;
    std::set<std::string> group_ids;
    for (const auto& entry : root.at("groups")) {
        GroupDefinition group{entry.at("id").get<std::string>(), entry.at("name").get<std::string>(),
                              entry.at("category").get<std::string>()};
        if (group.id.empty() || group.name.empty() || !group_ids.insert(group.id).second) {
            throw std::invalid_argument("Empty or duplicate character group");
        }
        roster.groups.push_back(std::move(group));
    }
    std::set<std::string> character_ids;
    std::set<std::string> assigned_skills;
    for (const auto& entry : root.at("characters")) {
        auto character = ParseCharacter(entry);
        if (!character_ids.insert(character.id).second || !group_ids.contains(character.group_id)) {
            throw std::invalid_argument("Duplicate character or unknown group: " + character.id);
        }
        for (const auto& skill_id : character.skill_ids) {
            const auto skill =
                std::find_if(cards.begin(), cards.end(), [&skill_id](const auto& card) { return card.id == skill_id; });
            if (skill == cards.end() || skill->kind != CardKind::SKILL || skill->character_id != character.id ||
                !assigned_skills.insert(skill_id).second || skill->cost > character.max_mana) {
                throw std::invalid_argument("Unknown, duplicate, foreign or unaffordable skill: " + skill_id);
            }
        }
        roster.characters.push_back(std::move(character));
    }
    for (const auto& card : cards) {
        if (!card.character_id.empty() && (!character_ids.contains(card.character_id) ||
                                           (card.kind == CardKind::SKILL && !assigned_skills.contains(card.id)))) {
            throw std::invalid_argument("Card has no matching character skill: " + card.id);
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        roster.player_character_ids[i] = root.at("players").at(i).get<std::string>();
        static_cast<void>(roster.character(roster.player_character_ids[i]));
    }
    return roster;
}
}  // namespace cardis
