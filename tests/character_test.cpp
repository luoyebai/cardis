#include <algorithm>
#include <chrono>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "cardis/content/catalog.hpp"
#include "cardis/runtime/runtime.hpp"

namespace {
void Check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void PassTwice(cardis::Game& game) {
    Check(game.pass(game.state().priority).accepted, "first pass failed");
    Check(game.pass(game.state().priority).accepted, "second pass failed");
}
void CheckDeckOwnership(const cardis::Game& game) {
    for (const auto owner : {cardis::PlayerId::FIRST, cardis::PlayerId::SECOND}) {
        const auto& player = game.state().players[cardis::Index(owner)];
        std::map<std::string, int> copies;
        int neutral_cards = 0;
        auto count = [&](std::size_t index) {
            const auto& card = game.cards().at(index);
            Check(card.character_id.empty() || card.character_id == player.character_id,
                  "foreign character card entered a player's zones");
            ++copies[card.id];
            if (card.character_id.empty()) {
                ++neutral_cards;
            }
        };
        for (const auto index : player.hand) {
            count(index);
        }
        for (const auto index : player.deck) {
            count(index);
        }
        for (const auto index : player.graveyard) {
            count(index);
        }
        for (const auto& unit : player.battlefield) {
            count(unit.card);
        }
        for (const auto& item : game.state().stack) {
            if (item.controller == owner && item.kind == cardis::StackKind::CAST) {
                count(item.card);
            }
        }
        Check(copies.size() == 15, "starter deck must contain 15 distinct definitions");
        for (const auto& [id, total] : copies) {
            Check(total == 2, "starter deck copy count changed for " + id);
        }
        Check(neutral_cards == 22, "shared neutral cards missing from starter deck");
    }
}

void CheckLoadouts(const std::filesystem::path& manifest) {
    auto runtime = cardis::CreateRuntime(manifest);
    auto& game = runtime->require<cardis::Game>();
    const auto& roster = runtime->require<cardis::CharacterRoster>();
    Check(game.state().players[0].character_id == "hitori_gotoh", "P1 must be Hitori");
    Check(game.state().players[1].character_id == "anon_chihaya", "P2 must be Anon");
    Check(roster.group(roster.character("anon_chihaya").group_id).name == "MyGO!!!!!", "wrong Anon group");
    Check(roster.character("hitori_gotoh").gender == cardis::Gender::FEMALE, "gender not loaded");
    for (const auto& player : game.state().players) {
        Check(player.hand.size() == 4 && player.deck.size() == 26, "opening draw must leave 4 hand and 26 deck cards");
        Check(player.life == 30 && player.max_life == 30 && player.mana_limit == 10,
              "roster starting life or resource limit ignored");
    }
    Check(game.state().players[0].max_mana == 1 && game.state().players[0].mana == 1 &&
              game.state().players[1].max_mana == 0 && game.state().players[1].mana == 0,
          "initial spirit must begin at 1 for active player and 0 for second player");
    CheckDeckOwnership(game);
    const auto initial_players = game.state().players;
    Check(!game.cast(cardis::PlayerId::FIRST, initial_players[0].hand.size(), cardis::Target{cardis::PlayerId::SECOND})
               .accepted,
          "catalog card outside hand accessible");

    bool neutral_cast = false;
    for (int step = 0; step < 120 && !neutral_cast; ++step) {
        const auto& state = game.state();
        if (state.phase == cardis::Phase::MAIN && state.active == state.priority && state.stack.empty()) {
            const auto owner = state.active;
            const auto& hand = state.players[cardis::Index(owner)].hand;
            for (std::size_t index = 0; index < hand.size(); ++index) {
                const auto& card = game.cards()[hand[index]];
                const auto target = card.effect == cardis::EffectKind::DAMAGE && card.kind == cardis::CardKind::SKILL
                                        ? cardis::Opponent(owner)
                                        : owner;
                if (card.character_id.empty() && game.canCast(owner, index, cardis::Target{target}).accepted) {
                    Check(game.cast(owner, index, cardis::Target{target}).accepted,
                          "legal shared neutral card rejected");
                    CheckDeckOwnership(game);
                    PassTwice(game);
                    CheckDeckOwnership(game);
                    neutral_cast = true;
                    break;
                }
            }
        }
        if (!neutral_cast) {
            Check(game.pass(game.state().priority).accepted, "progress toward neutral cast failed");
        }
    }
    Check(neutral_cast, "neither player could play a shared neutral card");
    game.reset();
    for (std::size_t index = 0; index < 2; ++index) {
        Check(game.state().players[index].hand == initial_players[index].hand &&
                  game.state().players[index].deck == initial_players[index].deck &&
                  game.state().players[index].character_id == initial_players[index].character_id,
              "reset failed to restore seeded opening cards or character identity");
    }
    Check(game.state().turn == 1 && game.state().phase == cardis::Phase::MAIN && game.state().stack.empty() &&
              game.state().players[0].battlefield.empty() && game.state().players[1].battlefield.empty(),
          "reset left stale match state");
    CheckDeckOwnership(game);
    cardis::Game repeated(game.cards(), {roster.character("hitori_gotoh"), roster.character("anon_chihaya")});
    Check(repeated.state().players[0].hand == initial_players[0].hand &&
              repeated.state().players[1].deck == initial_players[1].deck,
          "same seed must reproduce opening shuffle");
    auto custom = roster.character("hitori_gotoh");
    custom.starting_life = 28;
    custom.max_mana = 3;
    auto affordable_cards = game.cards();
    for (auto& card : affordable_cards) {
        card.cost = std::min(card.cost, custom.max_mana);
    }
    cardis::Game customized(affordable_cards, {custom, roster.character("anon_chihaya")});
    Check(customized.state().players[0].life == 28 && customized.state().players[0].mana == 1 &&
              customized.state().players[0].mana_limit == 3,
          "character stats are cosmetic instead of functional");
    for (int i = 0; i < 24; ++i) {
        PassTwice(customized);
        Check(customized.state().players[0].max_mana <= 3, "resource growth exceeded character limit");
    }
    Check(customized.state().players[0].mana == 3, "turn refresh ignored character max mana");
    customized.reset();
    Check(customized.state().players[0].life == 28, "reset ignored character starting life");
    runtime->registry().dispose("cardis.match");
    Check(runtime->use<cardis::CharacterRoster>() == nullptr, "roster service survived match unload");
    Check(runtime->use<cardis::Game>() == nullptr, "game service survived match unload");
}

void CheckInvalidReferences(const std::filesystem::path& manifest) {
    const auto cards = cardis::LoadCatalog(manifest);
    std::ifstream input(manifest.parent_path() / "characters.json");
    const auto original = nlohmann::json::parse(input);
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("cardis-roster-" + suffix + ".json");
    struct TemporaryFile {
        std::filesystem::path path;
        ~TemporaryFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    const std::vector<std::function<void(nlohmann::json&)>> invalid{
        [](auto& value) { value["characters"][0]["group_id"] = "missing"; },
        [](auto& value) { value["characters"][0]["gender"] = "invalid"; },
        [](auto& value) { value["characters"][0]["portrait"] = "../escape.png"; },
        [](auto& value) { value["characters"][0]["gameplay"]["skill_ids"][0] = "missing"; },
        [](auto& value) { value["characters"][0]["gameplay"]["skill_ids"][0] = "spotlight_riff"; },
        [](auto& value) { value["characters"][0]["gameplay"]["skill_ids"][1] = "ember_arc"; },
        [](auto& value) { value["characters"][0]["gameplay"]["starting_life"] = 0; },
        [](auto& value) { value["characters"][0]["gameplay"]["max_mana"] = 1.5; },
        [](auto& value) { value["characters"][0]["gameplay"]["max_mana"] = 1; },
        [](auto& value) { value["players"][1] = "missing"; },
        [](auto& value) { value["players"].push_back("hitori_gotoh"); },
        [](auto& value) { value["groups"].push_back(value["groups"][0]); },
        [](auto& value) { value["characters"].push_back(value["characters"][0]); }};
    for (const auto& corrupt : invalid) {
        auto value = original;
        corrupt(value);
        {
            std::ofstream output(path);
            output << value.dump();
        }
        bool rejected = false;
        try {
            static_cast<void>(cardis::LoadRoster(path, cards));
        } catch (const std::exception&) {
            rejected = true;
        }
        Check(rejected, "invalid character manifest accepted");
    }
}

void CheckInvalidCards(const std::filesystem::path& manifest) {
    std::ifstream input(manifest);
    const auto original = nlohmann::json::parse(input);
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("cardis-cards-" + suffix + ".json");
    struct TemporaryFile {
        std::filesystem::path path;
        ~TemporaryFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    auto character_card = [](nlohmann::json& value) -> nlohmann::json& {
        for (auto& card : value["cards"]) {
            if (card.value("kind", std::string("skill")) == "character") {
                return card;
            }
        }
        throw std::runtime_error("test catalog contains no character cards");
    };
    const std::vector<std::function<void(nlohmann::json&)>> invalid{
        [](auto& value) { value["cards"][0]["kind"] = "unknown"; },
        [](auto& value) { value["cards"][0]["cost"] = 1.5; },
        [](auto& value) { value["cards"][0]["cost"] = -1; },
        [](auto& value) { value["cards"][0]["cost"] = 11; },
        [character_card](auto& value) { character_card(value)["attack"] = 1.5; },
        [character_card](auto& value) { character_card(value)["attack"] = -1; },
        [character_card](auto& value) { character_card(value)["attack"] = 101; },
        [character_card](auto& value) { character_card(value)["health"] = 1.5; },
        [character_card](auto& value) { character_card(value)["health"] = 0; },
        [character_card](auto& value) { character_card(value)["health"] = 101; },
        [character_card](auto& value) { character_card(value)["guard"] = "true"; },
        [character_card](auto& value) { character_card(value)["haste"] = 1; },
        [](auto& value) { value["cards"].push_back(value["cards"][0]); },
        [](auto& value) { value["cards"][0]["effect"] = "unknown"; },
        [](auto& value) { value["cards"][0]["amount"] = 0; },
        [](auto& value) { value["cards"][0]["portrait"] = "../escape.png"; }};
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        auto value = original;
        invalid[index](value);
        {
            std::ofstream output(path);
            output << value.dump();
        }
        bool rejected = false;
        try {
            static_cast<void>(cardis::LoadCatalog(path));
        } catch (const std::exception&) {
            rejected = true;
        }
        Check(rejected, "invalid card manifest accepted in case " + std::to_string(index));
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        Check(argc == 2, "manifest argument required");
        CheckLoadouts(argv[1]);
        CheckInvalidReferences(argv[1]);
        CheckInvalidCards(argv[1]);
        std::cout << "Character ownership, rules, reset and manifest validation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
