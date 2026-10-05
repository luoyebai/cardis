#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

#include "cardis/core/game.hpp"

namespace cardis {
namespace {
bool ValidPlayer(PlayerId player) { return Index(player) < 2; }
bool ValidRow(Row row) { return row == Row::FRONT || row == Row::BACK; }
bool ValidEffect(EffectKind effect) {
    return effect == EffectKind::DAMAGE || effect == EffectKind::HEAL || effect == EffectKind::SHIELD;
}
std::size_t RowCount(const PlayerState& player, Row row) {
    return static_cast<std::size_t>(std::count_if(player.battlefield.begin(), player.battlefield.end(),
                                                  [row](const auto& unit) { return unit.row == row; }));
}
}  // namespace

PlayerId Opponent(PlayerId player) noexcept { return player == PlayerId::FIRST ? PlayerId::SECOND : PlayerId::FIRST; }
std::size_t Index(PlayerId player) noexcept { return static_cast<std::size_t>(player); }

std::vector<EffectStep> EffectsOf(const CardDefinition& card) {
    return card.effects.empty() ? std::vector<EffectStep>{{card.effect, card.amount}} : card.effects;
}

void ValidateCards(const std::vector<CardDefinition>& cards) {
    if (cards.empty() || cards.size() > 128) {
        throw std::invalid_argument("Card catalog must contain 1..128 cards");
    }
    std::set<std::string> ids;
    for (const auto& card : cards) {
        if (card.id.empty() || card.name.empty() || !ids.insert(card.id).second || card.cost < 0 || card.cost > 10 ||
            (card.kind != CardKind::SKILL && card.kind != CardKind::CHARACTER) || !ValidEffect(card.effect) ||
            card.effects.size() > 4 || (card.kind == CardKind::CHARACTER && !card.effects.empty()) ||
            (card.kind == CardKind::SKILL && (card.amount < 1 || card.amount > 100)) ||
            (card.kind == CardKind::CHARACTER &&
             (card.attack < 0 || card.attack > 100 || card.health < 1 || card.health > 100))) {
            throw std::invalid_argument("Invalid card rules: " + card.id);
        }
        if (card.kind == CardKind::SKILL) {
            std::optional<bool> selected_enemy;
            for (const auto& effect : EffectsOf(card)) {
                if (!ValidEffect(effect.effect) || effect.amount < 1 || effect.amount > 100 ||
                    (effect.timing != EffectTiming::ON_RESOLVE && effect.timing != EffectTiming::END_OF_TURN) ||
                    (effect.recipient != EffectRecipient::SELECTED && effect.recipient != EffectRecipient::CONTROLLER &&
                     effect.recipient != EffectRecipient::OPPONENT)) {
                    throw std::invalid_argument("Invalid skill effect: " + card.id);
                }
                if (effect.recipient == EffectRecipient::SELECTED) {
                    const bool enemy = effect.effect == EffectKind::DAMAGE;
                    if (selected_enemy.has_value() && selected_enemy.value() != enemy) {
                        throw std::invalid_argument("Selected effects must share a target side: " + card.id);
                    }
                    selected_enemy = enemy;
                }
            }
            if (!selected_enemy.has_value()) {
                throw std::invalid_argument("Skills require at least one selected-target effect: " + card.id);
            }
        }
    }
}

Game::Game(std::vector<CardDefinition> cards, const std::array<CharacterDefinition, 2>& characters, std::uint32_t seed)
    : cards_(std::move(cards)) {
    ValidateCards(cards_);
    std::mt19937 random(seed);
    for (std::size_t i = 0; i < characters.size(); ++i) {
        const auto& character = characters[i];
        if (character.id.empty() || character.starting_life < 1 || character.starting_life > 100 ||
            character.max_mana < 1 || character.max_mana > 10 || character.skill_ids.empty()) {
            throw std::invalid_argument("Invalid character loadout: " + character.id);
        }
        std::set<std::string> skills;
        for (const auto& skill_id : character.skill_ids) {
            const auto skill = std::find_if(cards_.begin(), cards_.end(),
                                            [&skill_id](const auto& card) { return card.id == skill_id; });
            if (skill == cards_.end() || skill->kind != CardKind::SKILL || skill->character_id != character.id ||
                !skills.insert(skill_id).second || skill->cost > character.max_mana) {
                throw std::invalid_argument("Invalid character skill: " + skill_id);
            }
        }
        auto& player = state_.players[i];
        player.character_id = character.id;
        player.life = character.starting_life;
        player.max_life = character.starting_life;
        player.mana_limit = character.max_mana;
        for (std::size_t card_index = 0; card_index < cards_.size(); ++card_index) {
            const auto& card = cards_[card_index];
            if (card.character_id.empty() || card.character_id == character.id) {
                if (card.cost > player.mana_limit) {
                    throw std::invalid_argument("Card exceeds the character's mana limit: " + card.id);
                }
                player.deck.push_back(card_index);
                player.deck.push_back(card_index);
            }
        }
        if (player.deck.size() != 30) {
            throw std::invalid_argument("Each character requires exactly 15 eligible card definitions (30 cards)");
        }
        // Explicit Fisher-Yates keeps replays stable across standard library implementations.
        for (std::size_t remaining = player.deck.size(); remaining > 1; --remaining) {
            const auto chosen = static_cast<std::size_t>(random()) % remaining;
            std::swap(player.deck[remaining - 1], player.deck[chosen]);
        }
        for (int count = 0; count < 4; ++count) {
            player.hand.push_back(player.deck.back());
            player.deck.pop_back();
        }
    }
    state_.players[0].max_mana = 1;
    state_.players[0].mana = 1;
    initial_state_ = state_;
}

const GameState& Game::state() const noexcept { return state_; }
const std::vector<CardDefinition>& Game::cards() const noexcept { return cards_; }
void Game::reset() {
    state_ = initial_state_;
    next_stack_id_ = 1;
    next_unit_id_ = 1;
    next_scheduled_id_ = 1;
}
const UnitState* Game::findUnit(Target target) const {
    if (!ValidPlayer(target.player) || target.unit == 0) {
        return nullptr;
    }
    const auto& units = state_.players[Index(target.player)].battlefield;
    const auto found =
        std::find_if(units.begin(), units.end(), [target](const auto& unit) { return unit.id == target.unit; });
    return found == units.end() ? nullptr : &*found;
}
UnitState* Game::findUnit(Target target) { return const_cast<UnitState*>(std::as_const(*this).findUnit(target)); }
bool Game::validTarget(Target target) const {
    return ValidPlayer(target.player) && (target.unit == 0 || findUnit(target) != nullptr);
}

ActionResult Game::canCast(PlayerId player, std::size_t hand_index, PlayerId target) const {
    return canCast(player, hand_index, Target{target});
}
ActionResult Game::canCast(PlayerId player, std::size_t hand_index, Target target, Row row) const {
    if (state_.phase == Phase::FINISHED) {
        return {false, "The game has ended"};
    }
    if (!ValidPlayer(player) || player != state_.priority || !ValidRow(row) || !validTarget(target)) {
        return {false, "Player does not have priority or target is invalid"};
    }
    const auto& owner = state_.players[Index(player)];
    if (hand_index >= owner.hand.size()) {
        return {false, "Card is not in hand"};
    }
    const auto& card = cards_[owner.hand[hand_index]];
    if (owner.mana < card.cost) {
        return {false, "Not enough mana"};
    }
    if (card.kind == CardKind::CHARACTER) {
        if (player != state_.active || state_.phase != Phase::MAIN || !state_.stack.empty() ||
            target.player != player || target.unit != 0) {
            return {false, "Characters require your main phase, an empty stack and your own side"};
        }
        if (RowCount(owner, row) >= 3) {
            return {false, "That row is full"};
        }
        for (const auto& unit : owner.battlefield) {
            if (!card.character_id.empty() && cards_[unit.card].character_id == card.character_id) {
                return {false, "That named character is already on your battlefield"};
            }
        }
    } else {
        for (const auto& effect : EffectsOf(card)) {
            if (effect.recipient == EffectRecipient::SELECTED &&
                target.player != (effect.effect == EffectKind::DAMAGE ? Opponent(player) : player)) {
                return {false, "This skill cannot target that side"};
            }
        }
    }
    return {true, {}};
}
ActionResult Game::cast(PlayerId player, std::size_t hand_index, PlayerId target) {
    return cast(player, hand_index, Target{target});
}
ActionResult Game::cast(PlayerId player, std::size_t hand_index, Target target, Row row) {
    const auto validation = canCast(player, hand_index, target, row);
    if (!validation.accepted) {
        return validation;
    }
    auto& owner = state_.players[Index(player)];
    const auto card_index = owner.hand[hand_index];
    const auto& card = cards_[card_index];
    state_.stack.push_back({next_stack_id_++, card_index, player, target, StackKind::CAST, 0, row, {}});
    owner.mana -= card.cost;
    owner.hand.erase(owner.hand.begin() + static_cast<std::ptrdiff_t>(hand_index));
    state_.consecutive_passes = 0;
    // Casting retains priority; the controller must explicitly pass to allow a response.
    record(EventKind::CAST, card.name + " entered the stack");
    return {true, {}};
}

bool Game::legalAttackTarget(PlayerId player, Target target) const {
    if (target.player != Opponent(player) || !validTarget(target)) {
        return false;
    }
    const auto& defender = state_.players[Index(target.player)];
    const bool has_guard =
        std::any_of(defender.battlefield.begin(), defender.battlefield.end(),
                    [this](const auto& unit) { return unit.row == Row::FRONT && cards_[unit.card].guard; });
    if (RowCount(defender, Row::FRONT) != 0) {
        const auto* unit = findUnit(target);
        return unit != nullptr && unit->row == Row::FRONT && (!has_guard || cards_[unit->card].guard);
    }
    return true;
}
ActionResult Game::canAttack(PlayerId player, std::uint64_t attacker, Target target) const {
    if (state_.phase != Phase::COMBAT || !ValidPlayer(player) || player != state_.active || player != state_.priority ||
        !state_.stack.empty()) {
        return {false, "Attacks require your combat phase, priority and an empty stack"};
    }
    const auto* unit = findUnit({player, attacker});
    if (unit == nullptr || unit->row != Row::FRONT || unit->exhausted || cards_[unit->card].attack == 0) {
        return {false, "That unit cannot attack"};
    }
    if (unit->summoned_turn == state_.turn && (!cards_[unit->card].haste || target.unit == 0)) {
        return {false, "New units must wait; Haste can attack units on entry"};
    }
    if (!legalAttackTarget(player, target)) {
        return {false, "Choose an enemy front-row guard first, then other front-row units"};
    }
    return {true, {}};
}
ActionResult Game::attack(PlayerId player, std::uint64_t attacker, Target target) {
    const auto validation = canAttack(player, attacker, target);
    if (!validation.accepted) {
        return validation;
    }
    auto* unit = findUnit({player, attacker});
    unit->exhausted = true;
    state_.stack.push_back({next_stack_id_++, unit->card, player, target, StackKind::ATTACK, attacker, unit->row, {}});
    state_.consecutive_passes = 0;
    record(EventKind::ATTACKED, cards_[unit->card].name + " declared an attack");
    return {true, {}};
}

ActionResult Game::canMove(PlayerId player, std::uint64_t unit_id) const {
    if (state_.phase != Phase::MAIN || !ValidPlayer(player) || player != state_.active || player != state_.priority ||
        !state_.stack.empty()) {
        return {false, "Moving requires your main phase, priority and an empty stack"};
    }
    const auto& owner = state_.players[Index(player)];
    const auto* unit = findUnit({player, unit_id});
    if (owner.moved_this_turn || unit == nullptr || unit->exhausted || unit->summoned_turn == state_.turn) {
        return {false, "Move one ready unit per turn after its summoning turn"};
    }
    if (RowCount(owner, unit->row == Row::FRONT ? Row::BACK : Row::FRONT) >= 3) {
        return {false, "The destination row is full"};
    }
    return {true, {}};
}
ActionResult Game::move(PlayerId player, std::uint64_t unit_id) {
    const auto validation = canMove(player, unit_id);
    if (!validation.accepted) {
        return validation;
    }
    auto* unit = findUnit({player, unit_id});
    unit->row = unit->row == Row::FRONT ? Row::BACK : Row::FRONT;
    unit->exhausted = true;
    state_.players[Index(player)].moved_this_turn = true;
    state_.consecutive_passes = 0;
    record(EventKind::MOVED, cards_[unit->card].name + " moved and became exhausted");
    return {true, {}};
}

ActionResult Game::pass(PlayerId player) {
    if (state_.phase == Phase::FINISHED || !ValidPlayer(player) || player != state_.priority) {
        return {false, "Player cannot pass now"};
    }
    record(EventKind::PASSED, "Player " + std::to_string(Index(player) + 1) + " passed");
    if (++state_.consecutive_passes == 2) {
        state_.consecutive_passes = 0;
        if (state_.stack.empty()) {
            advancePhase();
        } else {
            resolveTop();
        }
        state_.priority = state_.active;
    } else {
        state_.priority = Opponent(player);
    }
    return {true, {}};
}

void Game::applyDamage(Target target, int amount) {
    auto* unit = findUnit(target);
    auto& owner = state_.players[Index(target.player)];
    int& shield = unit == nullptr ? owner.shield : unit->shield;
    const int prevented = std::min(shield, amount);
    shield -= prevented;
    if (unit != nullptr) {
        unit->damage += amount - prevented;
    } else {
        owner.life -= amount - prevented;
    }
    if (prevented > 0) {
        record(EventKind::PREVENTED, "Shield prevented " + std::to_string(prevented) + " damage");
    }
}

void Game::applyEffect(Target target, const EffectStep& effect) {
    auto* unit = findUnit(target);
    auto& owner = state_.players[Index(target.player)];
    switch (effect.effect) {
        case EffectKind::DAMAGE:
            applyDamage(target, effect.amount);
            break;
        case EffectKind::HEAL:
            if (unit != nullptr) {
                unit->damage = std::max(0, unit->damage - effect.amount);
            } else {
                owner.life = std::min(owner.max_life, owner.life + effect.amount);
            }
            break;
        case EffectKind::SHIELD:
            if (unit != nullptr) {
                unit->shield += effect.amount;
            } else {
                owner.shield += effect.amount;
            }
            break;
    }
}

void Game::enqueueEndTriggers() {
    // APNAP: active player's objects enter first. LIFO therefore resolves the other player's group first.
    // Reverse registration within each group makes earlier registered effects resolve earlier for that owner.
    for (const auto controller : {state_.active, Opponent(state_.active)}) {
        for (auto it = state_.scheduled_effects.rbegin(); it != state_.scheduled_effects.rend(); ++it) {
            if (it->due_turn <= state_.turn && it->controller == controller) {
                state_.stack.push_back({next_stack_id_++, it->card, controller, it->target, StackKind::TRIGGER, 0,
                                        Row::FRONT, it->effect});
                record(EventKind::TRIGGERED, cards_[it->card].name + " end-of-turn effect entered the stack");
            }
        }
    }
    std::erase_if(state_.scheduled_effects, [this](const auto& effect) { return effect.due_turn <= state_.turn; });
}

void Game::resolveTop() {
    const auto item = state_.stack.back();
    state_.stack.pop_back();
    const auto& card = cards_[item.card];
    auto& owner = state_.players[Index(item.controller)];
    bool resolved = true;
    if (item.kind == StackKind::TRIGGER) {
        resolved = validTarget(item.target);
        if (resolved) {
            applyEffect(item.target, item.triggered_effect);
        }
    } else if (item.kind == StackKind::ATTACK) {
        const auto* source = findUnit({item.controller, item.attacker});
        resolved = source != nullptr && source->row == Row::FRONT && legalAttackTarget(item.controller, item.target);
        if (resolved) {
            const auto* defender = findUnit(item.target);
            const int counter_damage = defender == nullptr ? 0 : cards_[defender->card].attack;
            applyDamage(item.target, cards_[source->card].attack);
            // Both assignments precede death checks, including lethal counter-damage.
            if (defender != nullptr) {
                applyDamage({item.controller, item.attacker}, counter_damage);
            }
        }
    } else if (card.kind == CardKind::CHARACTER) {
        const bool duplicate =
            !card.character_id.empty() &&
            std::any_of(owner.battlefield.begin(), owner.battlefield.end(),
                        [this, &card](const auto& u) { return cards_[u.card].character_id == card.character_id; });
        resolved = RowCount(owner, item.row) < 3 && !duplicate;
        if (resolved) {
            owner.battlefield.push_back({next_unit_id_++, item.card, item.row, 0, 0, false, state_.turn});
        } else {
            owner.graveyard.push_back(item.card);
        }
    } else {
        resolved = validTarget(item.target);
        if (resolved) {
            for (const auto& effect : EffectsOf(card)) {
                Target target = item.target;
                if (effect.recipient == EffectRecipient::CONTROLLER) {
                    target = {item.controller};
                } else if (effect.recipient == EffectRecipient::OPPONENT) {
                    target = {Opponent(item.controller)};
                }
                if (effect.timing == EffectTiming::ON_RESOLVE) {
                    applyEffect(target, effect);
                } else {
                    const auto due_turn = state_.turn + (state_.phase == Phase::END ? 1U : 0U);
                    state_.scheduled_effects.push_back(
                        {next_scheduled_id_++, item.card, item.controller, target, effect, due_turn});
                    record(EventKind::SCHEDULED,
                           card.name + " scheduled an end-of-turn effect for turn " + std::to_string(due_turn));
                }
            }
        }
        owner.graveyard.push_back(item.card);
    }
    record(EventKind::RESOLVED, card.name + (resolved ? " resolved" : " fizzled: source or target is no longer legal"));
    checkStateActions();
}

void Game::advancePhase() {
    if (state_.phase == Phase::MAIN) {
        state_.phase = Phase::COMBAT;
    } else if (state_.phase == Phase::COMBAT) {
        state_.phase = Phase::END;
        enqueueEndTriggers();
    } else {
        auto& previous = state_.players[Index(state_.active)];
        while (previous.hand.size() > 8) {
            previous.graveyard.push_back(previous.hand.back());
            previous.hand.pop_back();
        }
        state_.active = Opponent(state_.active);
        ++state_.turn;
        state_.phase = Phase::MAIN;
        auto& active_player = state_.players[Index(state_.active)];
        active_player.max_mana = std::min(active_player.mana_limit, active_player.max_mana + 1);
        active_player.mana = active_player.max_mana;
        active_player.moved_this_turn = false;
        for (auto& unit : active_player.battlefield) {
            unit.exhausted = false;
        }
        draw(state_.active);
    }
    if (state_.phase != Phase::FINISHED) {
        record(EventKind::PHASE_CHANGED, "Phase advanced");
    }
}
void Game::draw(PlayerId player) {
    auto& owner = state_.players[Index(player)];
    if (owner.deck.empty()) {
        finish(Opponent(player), "Player " + std::to_string(Index(player) + 1) + " could not draw from an empty deck");
        return;
    }
    owner.hand.push_back(owner.deck.back());
    owner.deck.pop_back();
    record(EventKind::DRAWN, "Player " + std::to_string(Index(player) + 1) + " drew a card");
}
void Game::finish(std::optional<PlayerId> winner, std::string reason) {
    state_.phase = Phase::FINISHED;
    state_.winner = winner;
    state_.result = std::move(reason);
    record(EventKind::GAME_OVER, state_.result);
}
void Game::checkStateActions() {
    for (auto& player : state_.players) {
        auto& units = player.battlefield;
        for (auto it = units.begin(); it != units.end();) {
            if (it->damage >= cards_[it->card].health) {
                player.graveyard.push_back(it->card);
                record(EventKind::DIED, cards_[it->card].name + " entered the graveyard");
                it = units.erase(it);
            } else {
                ++it;
            }
        }
    }
    const bool first_dead = state_.players[0].life <= 0;
    const bool second_dead = state_.players[1].life <= 0;
    if (first_dead && second_dead) {
        finish(std::nullopt, "Draw: both heroes were defeated");
    } else if (first_dead || second_dead) {
        const auto winner = first_dead ? PlayerId::SECOND : PlayerId::FIRST;
        finish(winner, "Player " + std::to_string(Index(winner) + 1) + " won by defeating the enemy hero");
    }
}
void Game::record(EventKind kind, std::string message) {
    state_.events.push_back({state_.events.size() + 1, kind, std::move(message)});
}
}  // namespace cardis
