#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "cardis/content/catalog.hpp"

namespace cardis {
namespace {
EffectKind ParseEffect(const std::string& value) {
    if (value == "damage") {
        return EffectKind::DAMAGE;
    }
    if (value == "heal") {
        return EffectKind::HEAL;
    }
    if (value == "shield") {
        return EffectKind::SHIELD;
    }
    throw std::invalid_argument("Unknown effect: " + value);
}

CardKind ParseKind(const std::string& value) {
    if (value == "skill") {
        return CardKind::SKILL;
    }
    if (value == "character") {
        return CardKind::CHARACTER;
    }
    throw std::invalid_argument("Unknown card kind: " + value);
}

int ReadInteger(const nlohmann::json& value, int minimum, int maximum) {
    if (!value.is_number_integer() || value < minimum || value > maximum) {
        throw std::invalid_argument("Card stat must be a bounded integer");
    }
    return value.get<int>();
}
}  // namespace

std::vector<CardDefinition> LoadCatalog(const std::filesystem::path& manifest) {
    std::ifstream input(manifest);
    if (!input) {
        throw std::runtime_error("Cannot open card manifest: " + manifest.string());
    }
    const auto root = nlohmann::json::parse(input);
    if (root.at("schema_version") != 1 || !root.at("cards").is_array()) {
        throw std::invalid_argument("Unsupported card manifest schema");
    }
    std::vector<CardDefinition> cards;
    std::set<std::string> ids;
    for (const auto& entry : root.at("cards")) {
        CardDefinition card;
        card.id = entry.at("id").get<std::string>();
        card.name = entry.at("name").get<std::string>();
        card.character = entry.value("character", std::string{});
        card.character_id = entry.value("character_id", std::string{});
        card.portrait = entry.value("portrait", std::string{});
        card.artist = entry.value("artist", std::string{});
        card.license = entry.value("license", std::string{});
        card.kind = ParseKind(entry.value("kind", std::string("skill")));
        card.cost = ReadInteger(entry.at("cost"), 0, 10);
        if (card.kind == CardKind::CHARACTER) {
            card.attack = ReadInteger(entry.at("attack"), 0, 100);
            card.health = ReadInteger(entry.at("health"), 1, 100);
            card.guard = entry.value("guard", false);
            card.haste = entry.value("haste", false);
        } else {
            card.amount = ReadInteger(entry.at("amount"), 1, 100);
            card.effect = ParseEffect(entry.at("effect").get<std::string>());
        }
        if (card.id.empty() || card.name.empty() || !ids.insert(card.id).second) {
            throw std::invalid_argument("Empty or duplicate card identity: " + card.id);
        }
        const std::filesystem::path portrait(card.portrait);
        if (portrait.is_absolute() || portrait.has_root_name()) {
            throw std::invalid_argument("Portrait path must be relative: " + card.id);
        }
        for (const auto& part : portrait) {
            if (part == "..") {
                throw std::invalid_argument("Portrait path cannot escape the asset directory");
            }
        }
        cards.push_back(std::move(card));
    }
    // Reuse the rule boundary validation before publishing any content.
    ValidateCards(cards);
    return cards;
}
}  // namespace cardis
