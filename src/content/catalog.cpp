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

EffectTiming ParseTiming(const std::string& value) {
    if (value == "on_resolve") {
        return EffectTiming::ON_RESOLVE;
    }
    if (value == "end_of_turn") {
        return EffectTiming::END_OF_TURN;
    }
    throw std::invalid_argument("Unknown effect timing: " + value);
}

EffectRecipient ParseRecipient(const std::string& value) {
    if (value == "selected") {
        return EffectRecipient::SELECTED;
    }
    if (value == "controller") {
        return EffectRecipient::CONTROLLER;
    }
    if (value == "opponent") {
        return EffectRecipient::OPPONENT;
    }
    throw std::invalid_argument("Unknown effect recipient: " + value);
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
            if (entry.contains("effects")) {
                throw std::invalid_argument("Character effect sequences are not supported: " + card.id);
            }
            card.attack = ReadInteger(entry.at("attack"), 0, 100);
            card.health = ReadInteger(entry.at("health"), 1, 100);
            card.guard = entry.value("guard", false);
            card.haste = entry.value("haste", false);
        } else if (entry.contains("effects")) {
            const auto& effects = entry.at("effects");
            if (!effects.is_array() || effects.empty() || effects.size() > 4 || entry.contains("effect") ||
                entry.contains("amount")) {
                throw std::invalid_argument("Use either one effect or 1..4 effect steps: " + card.id);
            }
            for (const auto& step : effects) {
                card.effects.push_back({ParseEffect(step.at("effect").get<std::string>()),
                                        ReadInteger(step.at("amount"), 1, 100),
                                        ParseTiming(step.at("timing").get<std::string>()),
                                        ParseRecipient(step.value("recipient", std::string("selected")))});
            }
            // Legacy readers use the first selected effect for target selection and short summaries.
            const auto selected = std::find_if(card.effects.begin(), card.effects.end(), [](const auto& step) {
                return step.recipient == EffectRecipient::SELECTED;
            });
            if (selected != card.effects.end()) {
                card.effect = selected->effect;
                card.amount = selected->amount;
            }
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
