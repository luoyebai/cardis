#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "cardis/core/game.hpp"
#include "cardis/runtime/runtime.hpp"

namespace {
void RequireAccepted(const cardis::ActionResult& result) {
    if (!result.accepted) {
        throw std::runtime_error(result.error);
    }
}

bool TryCast(cardis::Game& game, bool characters) {
    const auto& state = game.state();
    const auto owner = state.priority;
    const auto& player = state.players[cardis::Index(owner)];
    const auto enemy = cardis::Opponent(owner);
    for (std::size_t index = 0; index < player.hand.size(); ++index) {
        const auto& card = game.cards()[player.hand[index]];
        if ((card.kind == cardis::CardKind::CHARACTER) != characters) {
            continue;
        }
        if (characters) {
            for (const auto row : {cardis::Row::FRONT, cardis::Row::BACK}) {
                if (game.canCast(owner, index, {owner}, row).accepted) {
                    RequireAccepted(game.cast(owner, index, {owner}, row));
                    return true;
                }
            }
            continue;
        }
        std::vector<cardis::Target> targets;
        if (card.effect == cardis::EffectKind::DAMAGE) {
            for (const auto& unit : state.players[cardis::Index(enemy)].battlefield) {
                targets.push_back({enemy, unit.id});
            }
            targets.push_back({enemy});
        } else {
            for (const auto& unit : player.battlefield) {
                if (card.effect == cardis::EffectKind::SHIELD || unit.damage > 0) {
                    targets.push_back({owner, unit.id});
                }
            }
            if (card.effect == cardis::EffectKind::SHIELD || player.life < player.max_life) {
                targets.push_back({owner});
            }
        }
        for (const auto target : targets) {
            if (game.canCast(owner, index, target).accepted) {
                RequireAccepted(game.cast(owner, index, target));
                return true;
            }
        }
    }
    return false;
}

bool TryDefend(cardis::Game& game) {
    const auto& state = game.state();
    if (state.stack.empty() || state.stack.back().kind != cardis::StackKind::ATTACK ||
        state.stack.back().target.player != state.priority) {
        return false;
    }
    const auto target = state.stack.back().target;
    const auto& hand = state.players[cardis::Index(state.priority)].hand;
    for (std::size_t index = 0; index < hand.size(); ++index) {
        const auto& card = game.cards()[hand[index]];
        if (card.kind == cardis::CardKind::SKILL && card.effect == cardis::EffectKind::SHIELD &&
            game.canCast(state.priority, index, target).accepted) {
            RequireAccepted(game.cast(state.priority, index, target));
            return true;
        }
    }
    return false;
}

bool TryMove(cardis::Game& game) {
    const auto owner = game.state().priority;
    for (const auto& unit : game.state().players[cardis::Index(owner)].battlefield) {
        if (unit.row == cardis::Row::BACK && game.canMove(owner, unit.id).accepted) {
            RequireAccepted(game.move(owner, unit.id));
            return true;
        }
    }
    return false;
}

bool TryAttack(cardis::Game& game) {
    const auto owner = game.state().priority;
    const auto enemy = cardis::Opponent(owner);
    std::vector<cardis::Target> targets{{enemy}};
    for (const auto& unit : game.state().players[cardis::Index(enemy)].battlefield) {
        targets.push_back({enemy, unit.id});
    }
    for (const auto& unit : game.state().players[cardis::Index(owner)].battlefield) {
        for (const auto target : targets) {
            if (game.canAttack(owner, unit.id, target).accepted) {
                RequireAccepted(game.attack(owner, unit.id, target));
                return true;
            }
        }
    }
    return false;
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        auto runtime = cardis::CreateRuntime(argc > 1 ? argv[1] : "assets/cards.json");
        auto& game = runtime->require<cardis::Game>();
        int actions = 0;
        int casts = 0;
        int attacks = 0;
        while (game.state().phase != cardis::Phase::FINISHED && actions < 2000) {
            ++actions;
            const auto& state = game.state();
            if (TryDefend(game)) {
                ++casts;
                continue;
            }
            if (state.priority == state.active && state.stack.empty()) {
                if (state.phase == cardis::Phase::MAIN) {
                    if (TryMove(game)) {
                        continue;
                    }
                    if (TryCast(game, true) || TryCast(game, false)) {
                        ++casts;
                        continue;
                    }
                } else if (state.phase == cardis::Phase::COMBAT && TryAttack(game)) {
                    ++attacks;
                    continue;
                }
            }
            RequireAccepted(game.pass(game.state().priority));
        }
        if (game.state().phase != cardis::Phase::FINISHED) {
            throw std::runtime_error("Autoplay exceeded 2000 actions without a result");
        }
        if (casts == 0 || attacks == 0 || game.state().players[0].max_mana < 2 ||
            game.state().players[1].max_mana < 2) {
            throw std::runtime_error("Autoplay did not exercise casting, combat and resource growth");
        }
        const auto winner = game.state().winner ? cardis::Index(*game.state().winner) + 1 : 0;
        std::cout << "Autoplay complete: seed=20261005 winner=" << winner << " turn=" << game.state().turn
                  << " actions=" << actions << " casts=" << casts << " attacks=" << attacks
                  << " life=" << game.state().players[0].life << ':' << game.state().players[1].life << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cardis: " << error.what() << '\n';
        return 1;
    }
}
