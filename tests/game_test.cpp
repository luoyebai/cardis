#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cardis/content/catalog.hpp"
#include "cardis/core/game.hpp"
#include "cardis/runtime/runtime.hpp"

namespace {
using cardis::CardDefinition;
using cardis::CardKind;
using cardis::EffectKind;
using cardis::EffectRecipient;
using cardis::EffectStep;
using cardis::EffectTiming;
using cardis::Game;
using cardis::Phase;
using cardis::PlayerId;
using cardis::Row;
using cardis::Target;
constexpr PlayerId FIRST = PlayerId::FIRST;
constexpr PlayerId SECOND = PlayerId::SECOND;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template <class Function>
void CheckThrows(Function function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, message);
}
void PassTwice(Game& game) {
    Check(game.pass(game.state().priority).accepted, "first pass rejected");
    Check(game.pass(game.state().priority).accepted, "second pass rejected");
}
void NextTurn(Game& game) {
    const auto turn = game.state().turn;
    for (int steps = 0; steps < 32 && game.state().turn == turn && game.state().phase != Phase::FINISHED; ++steps) {
        Check(game.pass(game.state().priority).accepted, "turn transition pass rejected");
    }
    Check(game.state().turn == turn + 1, "turn failed to advance");
}
std::array<cardis::CharacterDefinition, 2> Characters() {
    std::array<cardis::CharacterDefinition, 2> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].id = i == 0 ? "first" : "second";
        result[i].starting_life = 30;
        result[i].max_mana = 10;
        result[i].skill_ids = {i == 0 ? "card14" : "card15"};
    }
    return result;
}
std::vector<CardDefinition> Fixture() {
    std::vector<CardDefinition> cards(16);
    for (std::size_t i = 0; i < cards.size(); ++i) {
        auto& card = cards[i];
        card.id = "card" + std::to_string(i);
        card.name = card.id;
        card.cost = 0;
        card.amount = 3;
        if (i >= 3 && i < 14) {
            card.kind = CardKind::CHARACTER;
            card.attack = 3;
            card.health = 3;
        }
    }
    cards[1].effect = EffectKind::SHIELD;
    cards[2].effect = EffectKind::HEAL;
    cards[2].amount = 4;
    cards[4].attack = 1;
    cards[4].health = 5;
    cards[4].guard = true;
    cards[5].haste = true;
    cards[5].health = 2;
    cards[6].attack = 1;
    cards[6].health = 10;
    cards[7].attack = 5;
    cards[7].health = 5;
    cards[8].attack = 1;
    cards[8].health = 1;
    cards[14].character_id = "first";
    cards[15].character_id = "second";
    cards[15].effect = EffectKind::SHIELD;
    return cards;
}
bool Contains(const std::vector<std::size_t>& hand, const std::vector<std::size_t>& required) {
    for (const auto card : required) {
        if (std::count(hand.begin(), hand.end(), card) < std::count(required.begin(), required.end(), card)) {
            return false;
        }
    }
    return true;
}
// Select a reproducible legal opening, rather than adding mutable test-only game state.
Game Opening(const std::vector<CardDefinition>& cards, const std::vector<std::size_t>& first = {},
             const std::vector<std::size_t>& second = {}) {
    for (std::uint32_t seed = 0; seed < 200000U; ++seed) {
        Game game(cards, Characters(), seed);
        if (Contains(game.state().players[0].hand, first) && Contains(game.state().players[1].hand, second)) {
            return game;
        }
    }
    throw std::runtime_error("fixture could not find the required opening");
}
std::size_t InHand(const Game& game, PlayerId player, std::size_t card) {
    const auto& hand = game.state().players[cardis::Index(player)].hand;
    const auto found = std::find(hand.begin(), hand.end(), card);
    Check(found != hand.end(), "required card not in hand");
    return static_cast<std::size_t>(found - hand.begin());
}
void Cast(Game& game, PlayerId player, std::size_t card, Target target, Row row = Row::FRONT) {
    const auto result = game.cast(player, InHand(game, player, card), target, row);
    Check(result.accepted, "fixture cast rejected: " + result.error);
}
std::uint64_t Summon(Game& game, PlayerId player, std::size_t card, Row row = Row::FRONT) {
    Cast(game, player, card, {player}, row);
    PassTwice(game);
    return game.state().players[cardis::Index(player)].battlefield.back().id;
}
void CheckConservation(const Game& game) {
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& player = game.state().players[i];
        std::size_t total =
            player.deck.size() + player.hand.size() + player.graveyard.size() + player.battlefield.size();
        for (const auto& item : game.state().stack) {
            if (cardis::Index(item.controller) == i && item.kind == cardis::StackKind::CAST) {
                ++total;
            }
        }
        Check(total == 30, "cards duplicated or vanished between zones");
    }
}

void CheckSetupDrawAndReset() {
    const auto cards = Fixture();
    Game game(cards, Characters(), 1234);
    Game same(cards, Characters(), 1234);
    Game different(cards, Characters(), 1235);
    const auto opening = game.state();
    Check(opening.players[0].hand == same.state().players[0].hand, "shuffle is not deterministic");
    Check(opening.players[0].deck != different.state().players[0].deck, "seed did not affect shuffle");
    for (const auto& player : opening.players) {
        Check(player.hand.size() == 4 && player.deck.size() == 26 && player.life == 30, "opening setup incorrect");
        std::vector<std::size_t> all = player.deck;
        all.insert(all.end(), player.hand.begin(), player.hand.end());
        for (const auto card : all) {
            Check(std::count(all.begin(), all.end(), card) == 2, "deck must have exactly two copies");
            Check(cards[card].character_id.empty() || cards[card].character_id == player.character_id,
                  "opponent exclusive card entered deck");
        }
    }
    Check(opening.players[0].mana == 1 && opening.players[1].mana == 0, "initial spirit incorrect");
    NextTurn(game);
    Check(game.state().active == SECOND && game.state().players[1].hand.size() == 5 &&
              game.state().players[1].max_mana == 1,
          "second player first draw or spirit incorrect");
    NextTurn(game);
    Check(game.state().players[0].hand.size() == 5 && game.state().players[0].max_mana == 2,
          "first player next turn wrong");
    while (game.state().turn < 53) {
        NextTurn(game);
        CheckConservation(game);
        for (const auto& player : game.state().players) {
            Check(player.max_mana <= 10 && player.mana == player.max_mana, "spirit cap or refresh incorrect");
        }
    }
    Check(game.state().phase != Phase::FINISHED && game.state().players[0].deck.empty() &&
              game.state().players[1].deck.empty(),
          "drawing the last card must not lose the game");
    Check(game.state().players[1].hand.size() == 8 && !game.state().players[1].graveyard.empty(),
          "end phase failed to discard down to eight");
    NextTurn(game);
    Check(game.state().phase == Phase::FINISHED && game.state().winner == FIRST, "empty draw did not lose");
    Check(!game.pass(game.state().priority).accepted, "finished game accepted pass");
    game.reset();
    Check(game.state().turn == 1 && game.state().phase == Phase::MAIN && game.state().events.empty() &&
              !game.state().winner && game.state().result.empty() &&
              game.state().players[0].deck == opening.players[0].deck &&
              game.state().players[1].hand == opening.players[1].hand,
          "reset did not restore original match");
}

void CheckPriorityAndCosts() {
    auto cards = Fixture();
    for (auto& card : cards) {
        card.cost = 1;
    }
    cards[1].cost = 0;
    cards[1].amount = 4;
    auto game = Opening(cards, {0}, {1});
    const auto invalid = static_cast<PlayerId>(99);
    Check(!game.cast(SECOND, 0, FIRST).accepted && !game.cast(invalid, 0, FIRST).accepted &&
              !game.cast(FIRST, 99, SECOND).accepted && !game.cast(FIRST, InHand(game, FIRST, 0), FIRST).accepted &&
              !game.cast(FIRST, 0, Target{invalid}).accepted && !game.move(invalid, 1).accepted &&
              !game.attack(invalid, 1, {FIRST}).accepted && !game.pass(invalid).accepted,
          "illegal action accepted");
    Check(game.state().events.empty() && game.state().players[0].mana == 1, "invalid action mutated state");
    Cast(game, FIRST, 0, {SECOND});
    Check(game.state().players[0].mana == 0 && game.state().priority == FIRST, "cost or retained priority incorrect");
    bool checked_cost = false;
    for (std::size_t i = 0; i < game.state().players[0].hand.size(); ++i) {
        if (cards[game.state().players[0].hand[i]].cost > 0) {
            Check(!game.cast(FIRST, i, FIRST).accepted, "unaffordable card accepted");
            checked_cost = true;
        }
    }
    Check(checked_cost, "fixture lacked a card for the insufficient spirit check");
    Check(game.pass(FIRST).accepted, "caster could not pass");
    Cast(game, SECOND, 1, {SECOND});
    Check(game.state().consecutive_passes == 0, "response did not reset passes");
    PassTwice(game);
    Check(game.state().stack.size() == 1 && game.state().players[1].shield == 4 && game.state().players[1].life == 30 &&
              game.state().priority == FIRST,
          "LIFO must resolve only the response and return active priority");
    PassTwice(game);
    Check(game.state().players[1].shield == 1 && game.state().players[1].life == 30, "shield prevention incorrect");
    NextTurn(game);
    Check(game.state().players[1].shield == 1, "persistent shield was incorrectly removed at turn end");
    CheckConservation(game);
    for (std::size_t i = 0; i < game.state().events.size(); ++i) {
        Check(game.state().events[i].sequence == i + 1, "event sequence unstable");
    }
}

void CheckCombatAndDeaths() {
    auto game = Opening(Fixture(), {3}, {3});
    const auto first = Summon(game, FIRST, 3);
    Check(!game.attack(FIRST, first, {SECOND}).accepted, "attack allowed in main phase");
    Check(!game.move(FIRST, first).accepted, "new unit allowed to move");
    PassTwice(game);
    Check(game.state().phase == Phase::COMBAT && !game.attack(FIRST, first, {SECOND}).accepted,
          "summoning sickness missing");
    NextTurn(game);
    const auto second = Summon(game, SECOND, 3);
    NextTurn(game);
    PassTwice(game);
    Check(!game.attack(FIRST, first, {SECOND}).accepted, "front row failed to protect hero");
    Check(!game.attack(FIRST, second, {SECOND, second}).accepted, "attacked with opponent unit");
    Check(game.attack(FIRST, first, {SECOND, second}).accepted, "legal attack rejected");
    Check(game.state().players[0].battlefield[0].exhausted, "attack did not exhaust source");
    Check(!game.attack(FIRST, first, {SECOND, second}).accepted, "second attack accepted with stack pending");
    PassTwice(game);
    Check(game.state().players[0].battlefield.empty() && game.state().players[1].battlefield.empty() &&
              game.state().players[0].graveyard.size() == 1 && game.state().players[1].graveyard.size() == 1,
          "lethal exchange was not simultaneous");
    CheckConservation(game);
}

void CheckGuardHasteAndRows() {
    auto guard = Opening(Fixture(), {7}, {4, 8, 6});
    const auto attacker = Summon(guard, FIRST, 7);
    NextTurn(guard);
    const auto protector = Summon(guard, SECOND, 4);
    const auto other = Summon(guard, SECOND, 8);
    const auto back = Summon(guard, SECOND, 6, Row::BACK);
    NextTurn(guard);
    PassTwice(guard);
    Check(!guard.canAttack(FIRST, attacker, {SECOND, other}).accepted &&
              !guard.canAttack(FIRST, attacker, {SECOND, back}).accepted &&
              !guard.canAttack(FIRST, attacker, {SECOND}).accepted &&
              guard.canAttack(FIRST, attacker, {SECOND, protector}).accepted,
          "front guard targeting rules incorrect");
    Check(guard.attack(FIRST, attacker, {SECOND, protector}).accepted, "guard attack rejected");
    PassTwice(guard);
    Check(guard.state().players[1].battlefield.size() == 2 && guard.state().players[0].battlefield[0].damage == 1,
          "guard exchange wrong");

    auto haste = Opening(Fixture(), {8}, {5});
    const auto weak = Summon(haste, FIRST, 8, Row::BACK);
    NextTurn(haste);
    const auto runner = Summon(haste, SECOND, 5);
    PassTwice(haste);
    Check(!haste.canAttack(SECOND, runner, {FIRST}).accepted, "new haste unit attacked hero");
    Check(haste.attack(SECOND, runner, {FIRST, weak}).accepted, "haste could not attack exposed back row");
    PassTwice(haste);
    Check(haste.state().players[0].battlefield.empty() && haste.state().players[1].battlefield[0].damage == 1,
          "haste exchange incorrect");
    Check(!haste.canAttack(SECOND, runner, {FIRST}).accepted, "exhausted unit attacked twice");

    auto rows = Opening(Fixture(), {3, 4, 6, 7});
    const auto movable = Summon(rows, FIRST, 3);
    const auto another = Summon(rows, FIRST, 4);
    Summon(rows, FIRST, 6);
    Check(!rows.canCast(FIRST, InHand(rows, FIRST, 7), Target{FIRST}, Row::FRONT).accepted,
          "fourth front unit accepted");
    const auto rear = Summon(rows, FIRST, 7, Row::BACK);
    NextTurn(rows);
    Check(rows.pass(SECOND).accepted, "priority pass failed");
    Check(!rows.canMove(FIRST, movable).accepted, "opponent turn move accepted");
    NextTurn(rows);
    Check(rows.move(FIRST, movable).accepted && rows.state().players[0].moved_this_turn,
          "ready unit could not change row");
    Check(!rows.move(FIRST, another).accepted, "second move in same turn accepted");
    PassTwice(rows);
    Check(!rows.canAttack(FIRST, rear, {SECOND}).accepted && !rows.canAttack(FIRST, movable, {SECOND}).accepted,
          "rear or just-moved unit attacked");
}

void CheckStaleTargetsAndSource() {
    auto cards = Fixture();
    cards[0].cost = 1;
    auto game = Opening(cards, {0, 0}, {8});
    NextTurn(game);
    const auto target = Summon(game, SECOND, 8);
    NextTurn(game);
    Cast(game, FIRST, 0, {SECOND, target});
    Cast(game, FIRST, 0, {SECOND, target});
    PassTwice(game);
    Check(game.state().players[1].battlefield.empty() && game.state().stack.size() == 1,
          "top spell failed to remove unit");
    PassTwice(game);
    Check(game.state().players[1].life == 30 && game.state().players[0].mana == 0 &&
              game.state().players[0].graveyard.size() == 2 && game.state().stack.empty(),
          "stale target redirected or refunded spell");
    CheckConservation(game);

    auto source = Opening(Fixture(), {3}, {0});
    const auto attacker = Summon(source, FIRST, 3);
    NextTurn(source);
    NextTurn(source);
    PassTwice(source);
    Check(source.attack(FIRST, attacker, {SECOND}).accepted, "hero attack rejected");
    Check(source.pass(FIRST).accepted, "attack priority pass failed");
    Cast(source, SECOND, 0, {FIRST, attacker});
    PassTwice(source);
    Check(source.state().players[0].battlefield.empty() && source.state().stack.size() == 1,
          "response did not remove source");
    PassTwice(source);
    Check(source.state().players[1].life == 30, "attack resolved after source died");
    CheckConservation(source);
}

void CheckHealingAndUnitShield() {
    auto game = Opening(Fixture(), {6, 1, 2}, {0});
    const auto unit = Summon(game, FIRST, 6);
    NextTurn(game);
    Cast(game, SECOND, 0, {FIRST, unit});
    PassTwice(game);
    Check(game.state().players[0].battlefield[0].damage == 3, "unit did not retain marked damage");
    Check(game.pass(SECOND).accepted, "defender did not receive response priority");
    Cast(game, FIRST, 2, {FIRST, unit});
    PassTwice(game);
    Check(game.state().players[0].battlefield[0].damage == 0, "unit healing underflowed or failed");
    Check(game.pass(SECOND).accepted, "shield priority pass failed");
    Cast(game, FIRST, 1, {FIRST, unit});
    PassTwice(game);
    NextTurn(game);
    Check(game.state().players[0].battlefield[0].shield == 3, "unit shield failed to persist");

    auto cards = Fixture();
    cards[0].amount = 100;
    auto lethal = Opening(cards, {0, 2});
    Cast(lethal, FIRST, 2, {FIRST});
    PassTwice(lethal);
    Check(lethal.state().players[0].life == 30, "healing exceeded max life");
    Cast(lethal, FIRST, 0, {SECOND});
    PassTwice(lethal);
    Check(lethal.state().phase == Phase::FINISHED && lethal.state().winner == FIRST && !lethal.state().result.empty(),
          "lethal damage did not produce a result");
    Check(!lethal.cast(FIRST, 0, SECOND).accepted && !lethal.pass(FIRST).accepted, "finished game accepted commands");
}

void CheckNamedUniquenessAndValidation() {
    auto cards = Fixture();
    cards[13].character_id = "first";
    auto counterpart = cards[13];
    counterpart.id = "second_unit";
    counterpart.name = counterpart.id;
    counterpart.character_id = "second";
    cards.push_back(counterpart);
    auto game = Opening(cards, {13, 13});
    Summon(game, FIRST, 13);
    Check(!game.canCast(FIRST, InHand(game, FIRST, 13), FIRST).accepted, "duplicate named character accepted");
    auto invalid = Fixture();
    invalid[0].id = invalid[1].id;
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "duplicate id accepted");
    invalid = Fixture();
    invalid[3].health = 0;
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "zero-health character accepted");
    invalid = Fixture();
    invalid.pop_back();
    CheckThrows([&] { Game bad(invalid, Characters()); }, "missing loadout skill accepted");
    invalid = Fixture();
    invalid.erase(invalid.begin() + 3);
    CheckThrows([&] { Game bad(invalid, Characters()); }, "non-thirty-card deck accepted");
    auto characters = Characters();
    characters[0].skill_ids = {"missing"};
    CheckThrows([&] { Game bad(Fixture(), characters); }, "missing skill reference accepted");
}

void CheckMixedEffectsAndEndResponses() {
    auto cards = Fixture();
    cards[0].cost = 1;
    cards[0].effects = {{EffectKind::DAMAGE, 2, EffectTiming::ON_RESOLVE, EffectRecipient::SELECTED},
                        {EffectKind::HEAL, 1, EffectTiming::ON_RESOLVE, EffectRecipient::CONTROLLER},
                        {EffectKind::DAMAGE, 3, EffectTiming::END_OF_TURN, EffectRecipient::SELECTED},
                        {EffectKind::SHIELD, 2, EffectTiming::END_OF_TURN, EffectRecipient::CONTROLLER}};
    cards[15].effect = EffectKind::DAMAGE;
    auto game = Opening(cards, {0}, {15, 1});
    Check(game.pass(FIRST).accepted, "opening response priority failed");
    Cast(game, SECOND, 15, {FIRST});
    PassTwice(game);
    Check(game.state().players[0].life == 27, "mixed effect fixture failed to create missing life");
    Check(!game.canCast(FIRST, InHand(game, FIRST, 0), FIRST).accepted,
          "mixed effect card accepted the wrong selected side");
    Cast(game, FIRST, 0, {SECOND});
    PassTwice(game);
    Check(game.state().players[0].life == 28 && game.state().players[1].life == 28 &&
              game.state().players[0].mana == 0 && game.state().scheduled_effects.size() == 2,
          "mixed immediate effects or delayed registration incorrect");
    Check(game.state().players[0].graveyard.size() == 1, "original mixed spell did not enter graveyard once");
    PassTwice(game);
    Check(game.state().phase == Phase::COMBAT && game.state().stack.empty() && game.state().players[1].life == 28,
          "end effects triggered before end phase");
    PassTwice(game);
    Check(game.state().phase == Phase::END && game.state().scheduled_effects.empty() &&
              game.state().stack.size() == 2 && game.state().stack.back().kind == cardis::StackKind::TRIGGER &&
              game.state().players[1].life == 28,
          "end effects must enter stack before dealing damage");
    Check(game.pass(FIRST).accepted, "end trigger response priority failed");
    Cast(game, SECOND, 1, {SECOND});
    PassTwice(game);
    Check(game.state().players[1].shield == 3 && game.state().stack.size() == 2,
          "response did not resolve ahead of delayed triggers");
    PassTwice(game);
    Check(game.state().players[1].life == 28 && game.state().players[1].shield == 0 &&
              game.state().players[0].shield == 0 && game.state().stack.size() == 1,
          "first delayed effect ignored shield or resolved multiple triggers at once");
    PassTwice(game);
    Check(game.state().players[0].shield == 2 && game.state().stack.empty() && game.state().players[0].mana == 0 &&
              game.state().players[0].graveyard.size() == 1,
          "controller trigger failed or charged the spell twice");
    CheckConservation(game);

    game.reset();
    Cast(game, FIRST, 0, {SECOND});
    PassTwice(game);
    Check(game.state().scheduled_effects.size() == 2, "reset fixture did not schedule effects");
    game.reset();
    Check(game.state().scheduled_effects.empty() && game.state().stack.empty(), "reset retained delayed effects");
    PassTwice(game);
    PassTwice(game);
    Check(game.state().stack.empty() && game.state().players[0].shield == 0 && game.state().players[1].life == 30,
          "reset effects leaked into the next match");
}

void CheckDelayedStaleTargets() {
    auto cards = Fixture();
    cards[0].effects = {{EffectKind::DAMAGE, 1, EffectTiming::ON_RESOLVE, EffectRecipient::SELECTED},
                        {EffectKind::DAMAGE, 2, EffectTiming::END_OF_TURN, EffectRecipient::SELECTED}};
    auto stale_cast = Opening(cards, {0, 14}, {8});
    NextTurn(stale_cast);
    const auto original_target = Summon(stale_cast, SECOND, 8);
    NextTurn(stale_cast);
    Cast(stale_cast, FIRST, 0, {SECOND, original_target});
    Cast(stale_cast, FIRST, 14, {SECOND, original_target});
    PassTwice(stale_cast);
    PassTwice(stale_cast);
    Check(stale_cast.state().players[1].battlefield.empty() && stale_cast.state().scheduled_effects.empty() &&
              stale_cast.state().players[1].life == 30 && stale_cast.state().players[0].graveyard.size() == 2,
          "stale original target scheduled future effects or redirected damage");
    CheckConservation(stale_cast);

    auto stale_trigger = Opening(cards, {0, 14}, {3});
    NextTurn(stale_trigger);
    const auto delayed_target = Summon(stale_trigger, SECOND, 3);
    NextTurn(stale_trigger);
    Cast(stale_trigger, FIRST, 0, {SECOND, delayed_target});
    PassTwice(stale_trigger);
    Check(stale_trigger.state().scheduled_effects.size() == 1 &&
              stale_trigger.state().players[1].battlefield[0].damage == 1,
          "delayed unit target not registered after immediate damage");
    Cast(stale_trigger, FIRST, 14, {SECOND, delayed_target});
    PassTwice(stale_trigger);
    Check(stale_trigger.state().players[1].battlefield.empty(), "delayed target survived fixture removal");
    PassTwice(stale_trigger);
    PassTwice(stale_trigger);
    Check(stale_trigger.state().stack.size() == 1, "delayed effect skipped its response window");
    PassTwice(stale_trigger);
    Check(stale_trigger.state().players[1].life == 30 && stale_trigger.state().stack.empty() &&
              stale_trigger.state().players[0].graveyard.size() == 2,
          "dead delayed unit target became hero damage or duplicated source card");
    CheckConservation(stale_trigger);
}

void CheckEffectsCreatedDuringEnd() {
    auto cards = Fixture();
    cards[0].effects = {{EffectKind::DAMAGE, 1, EffectTiming::ON_RESOLVE, EffectRecipient::SELECTED},
                        {EffectKind::DAMAGE, 2, EffectTiming::END_OF_TURN, EffectRecipient::OPPONENT}};
    auto game = Opening(cards, {0});
    PassTwice(game);
    PassTwice(game);
    Check(game.state().phase == Phase::END, "fixture did not reach end phase");
    Cast(game, FIRST, 0, {SECOND});
    PassTwice(game);
    Check(game.state().players[1].life == 29 && game.state().scheduled_effects.size() == 1 &&
              game.state().scheduled_effects[0].due_turn == 2,
          "effect created during end must wait for the next turn's end event");
    PassTwice(game);
    Check(game.state().turn == 2 && game.state().phase == Phase::MAIN && game.state().stack.empty() &&
              game.state().players[1].life == 29,
          "end-created effect incorrectly resolved on the same end step");
    PassTwice(game);
    PassTwice(game);
    Check(game.state().stack.size() == 1 && game.state().players[1].life == 29,
          "next end event did not create a respondable trigger");
    PassTwice(game);
    Check(game.state().players[1].life == 27 && game.state().players[0].graveyard.size() == 1,
          "next-turn trigger failed or duplicated original card");
    CheckConservation(game);
}

void CheckEndTriggerOrder() {
    auto cards = Fixture();
    cards[0].effects = {{EffectKind::DAMAGE, 1, EffectTiming::END_OF_TURN, EffectRecipient::SELECTED},
                        {EffectKind::DAMAGE, 2, EffectTiming::END_OF_TURN, EffectRecipient::SELECTED}};
    auto game = Opening(cards, {0}, {0});
    Cast(game, FIRST, 0, {SECOND});
    PassTwice(game);
    Check(game.pass(FIRST).accepted, "nonactive spell priority failed");
    Cast(game, SECOND, 0, {FIRST});
    PassTwice(game);
    PassTwice(game);
    PassTwice(game);
    Check(game.state().stack.size() == 4 && game.state().stack.back().controller == SECOND,
          "nonactive player's end triggers must resolve before active player's triggers");
    PassTwice(game);
    Check(game.state().players[0].life == 29 && game.state().players[1].life == 30 && game.state().stack.size() == 3,
          "first registered nonactive trigger did not resolve first");
    PassTwice(game);
    Check(game.state().players[0].life == 27 && game.state().stack.size() == 2 &&
              game.state().stack.back().controller == FIRST,
          "second nonactive trigger did not precede active player's triggers");
    PassTwice(game);
    Check(game.state().players[1].life == 29 && game.state().stack.size() == 1,
          "first registered active trigger did not resolve first");
    PassTwice(game);
    Check(game.state().players[1].life == 27 && game.state().stack.empty(), "last active trigger failed");
    CheckConservation(game);
}

void CheckEffectValidation() {
    auto cards = Fixture();
    const auto legacy = cardis::EffectsOf(cards[0]);
    Check(legacy.size() == 1 && legacy[0].effect == EffectKind::DAMAGE && legacy[0].amount == 3 &&
              legacy[0].timing == EffectTiming::ON_RESOLVE && legacy[0].recipient == EffectRecipient::SELECTED,
          "legacy skill effect normalization changed");
    const EffectStep selected{EffectKind::DAMAGE, 2, EffectTiming::ON_RESOLVE, EffectRecipient::SELECTED};
    cards[0].effects = {selected};
    cardis::ValidateCards(cards);
    auto invalid = cards;
    invalid[0].effects[0].effect = static_cast<EffectKind>(99);
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "unknown effect enum accepted");
    invalid = cards;
    invalid[0].effects[0].timing = static_cast<EffectTiming>(99);
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "unknown effect timing accepted");
    invalid = cards;
    invalid[0].effects[0].recipient = static_cast<EffectRecipient>(99);
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "unknown effect recipient accepted");
    invalid = cards;
    invalid[0].effects[0].amount = 0;
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "zero effect amount accepted");
    invalid = cards;
    invalid[0].effects[0].amount = 101;
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "out-of-range effect amount accepted");
    invalid = cards;
    invalid[0].effects.assign(5, selected);
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "more than four effect steps accepted");
    invalid = cards;
    invalid[0].effects[0].recipient = EffectRecipient::OPPONENT;
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "skill without selected target accepted");
    invalid = cards;
    invalid[0].effects.push_back({EffectKind::HEAL, 1, EffectTiming::END_OF_TURN, EffectRecipient::SELECTED});
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "mixed friendly and enemy selected targets accepted");
    invalid = cards;
    invalid[3].effects = {selected};
    CheckThrows([&] { cardis::ValidateCards(invalid); }, "unsupported character spell effects accepted");
    cards[0].effect = EffectKind::SHIELD;
    auto game = Opening(cards, {0});
    Check(game.canCast(FIRST, InHand(game, FIRST, 0), SECOND).accepted &&
              !game.canCast(FIRST, InHand(game, FIRST, 0), FIRST).accepted,
          "explicit effect selected side incorrectly used legacy effect fields");
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        CheckSetupDrawAndReset();
        CheckPriorityAndCosts();
        CheckCombatAndDeaths();
        CheckGuardHasteAndRows();
        CheckStaleTargetsAndSource();
        CheckHealingAndUnitShield();
        CheckNamedUniquenessAndValidation();
        CheckMixedEffectsAndEndResponses();
        CheckDelayedStaleTargets();
        CheckEffectsCreatedDuringEnd();
        CheckEndTriggerOrder();
        CheckEffectValidation();
        Check(argc == 2, "manifest argument required");
        cardis::ValidateCards(cardis::LoadCatalog(argv[1]));
        auto runtime = cardis::CreateRuntime(argv[1]);
        Check(runtime->use<Game>() != nullptr, "Cordis match service missing");
        runtime->registry().dispose("cardis.match");
        Check(runtime->use<Game>() == nullptr, "Cordis unload did not remove service");
        std::cout << "All Cardis duel, rules, content and lifecycle checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
