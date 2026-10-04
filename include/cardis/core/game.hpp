#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cardis/core/character.hpp"

namespace cardis {

enum class PlayerId { FIRST, SECOND };
enum class Phase { MAIN, COMBAT, END, FINISHED };
enum class CardKind { SKILL, CHARACTER };
enum class Row { FRONT, BACK };
enum class StackKind { CAST, ATTACK };
enum class EffectKind { DAMAGE, HEAL, SHIELD };
enum class EventKind { CAST, PASSED, RESOLVED, PREVENTED, PHASE_CHANGED, GAME_OVER, ATTACKED, MOVED, DRAWN, DIED };

struct CardDefinition {
    std::string id;
    std::string name;
    std::string character;
    std::string portrait;
    std::string artist;
    std::string license;
    int cost = 1;
    int amount = 1;
    EffectKind effect = EffectKind::DAMAGE;
    std::string character_id;
    CardKind kind = CardKind::SKILL;
    int attack = 0;
    int health = 0;
    bool guard = false;
    bool haste = false;
};

struct Target {
    PlayerId player = PlayerId::FIRST;
    std::uint64_t unit = 0;  // Zero identifies the player's hero.
};

struct UnitState {
    std::uint64_t id = 0;
    std::size_t card = 0;
    Row row = Row::FRONT;
    int damage = 0;
    int shield = 0;
    bool exhausted = false;
    std::uint64_t summoned_turn = 0;
};

struct PlayerState {
    std::string character_id;
    int life = 30;
    int max_life = 30;
    int mana = 0;
    int max_mana = 0;
    int mana_limit = 10;
    int shield = 0;
    bool moved_this_turn = false;
    std::vector<std::size_t> deck;
    std::vector<std::size_t> hand;
    std::vector<std::size_t> graveyard;
    std::vector<UnitState> battlefield;
};

struct StackItem {
    std::uint64_t id = 0;
    std::size_t card = 0;
    PlayerId controller = PlayerId::FIRST;
    Target target;
    StackKind kind = StackKind::CAST;
    std::uint64_t attacker = 0;
    Row row = Row::FRONT;
};

struct GameEvent {
    std::uint64_t sequence = 0;
    EventKind kind = EventKind::PASSED;
    std::string message;
};

struct GameState {
    std::array<PlayerState, 2> players;
    PlayerId active = PlayerId::FIRST;
    PlayerId priority = PlayerId::FIRST;
    Phase phase = Phase::MAIN;
    std::uint64_t turn = 1;
    int consecutive_passes = 0;
    std::optional<PlayerId> winner;
    std::string result;
    std::vector<StackItem> stack;
    std::vector<GameEvent> events;
};

struct ActionResult {
    bool accepted = false;
    std::string error;
};

[[nodiscard]] PlayerId Opponent(PlayerId player) noexcept;
[[nodiscard]] std::size_t Index(PlayerId player) noexcept;
void ValidateCards(const std::vector<CardDefinition>& cards);

// Owns all rule mutations. UI and Cordis may only submit commands and read snapshots.
class Game {
 public:
    Game(std::vector<CardDefinition> cards, const std::array<CharacterDefinition, 2>& characters,
         std::uint32_t seed = 20261005U);
    [[nodiscard]] const GameState& state() const noexcept;
    [[nodiscard]] const std::vector<CardDefinition>& cards() const noexcept;
    [[nodiscard]] ActionResult canCast(PlayerId player, std::size_t hand_index, Target target,
                                       Row row = Row::FRONT) const;
    [[nodiscard]] ActionResult canCast(PlayerId player, std::size_t hand_index, PlayerId target) const;
    [[nodiscard]] ActionResult cast(PlayerId player, std::size_t hand_index, Target target, Row row = Row::FRONT);
    [[nodiscard]] ActionResult cast(PlayerId player, std::size_t hand_index, PlayerId target);
    [[nodiscard]] ActionResult canAttack(PlayerId player, std::uint64_t attacker, Target target) const;
    [[nodiscard]] ActionResult attack(PlayerId player, std::uint64_t attacker, Target target);
    [[nodiscard]] ActionResult canMove(PlayerId player, std::uint64_t unit) const;
    [[nodiscard]] ActionResult move(PlayerId player, std::uint64_t unit);
    [[nodiscard]] ActionResult pass(PlayerId player);
    void reset();

 private:
    [[nodiscard]] const UnitState* findUnit(Target target) const;
    [[nodiscard]] UnitState* findUnit(Target target);
    [[nodiscard]] bool validTarget(Target target) const;
    [[nodiscard]] bool legalAttackTarget(PlayerId player, Target target) const;
    void applyDamage(Target target, int amount);
    void resolveTop();
    void advancePhase();
    void draw(PlayerId player);
    void finish(std::optional<PlayerId> winner, std::string reason);
    void checkStateActions();
    void record(EventKind kind, std::string message);
    std::vector<CardDefinition> cards_;
    GameState state_;
    GameState initial_state_;
    std::uint64_t next_stack_id_ = 1;
    std::uint64_t next_unit_id_ = 1;
};

}  // namespace cardis
