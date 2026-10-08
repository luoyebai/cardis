// Headless checks for the client's presentation layer: easing curves,
// retargetable transitions, effect clocks, the particle budget, shake decay
// and the HUD geometry. Nothing here touches a graphics context, which is the
// point: the motion and the layout are verified without a GPU.
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "effects.hpp"
#include "layout.hpp"

namespace {
namespace fx = cardis::fx;
namespace ui = cardis::ui;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void CheckNear(float actual, float expected, float tolerance, const std::string& message) {
    Check(std::abs(actual - expected) <= tolerance, message);
}
bool Monotonic(float (*curve)(float)) {
    float previous = -1.0F;
    for (int step = 0; step <= 64; ++step) {
        const float value = curve(static_cast<float>(step) / 64.0F);
        if (value + 1e-4F < previous) {
            return false;
        }
        previous = value;
    }
    return true;
}

void CheckEasing() {
    CheckNear(fx::EaseOutCubic(0.0F), 0.0F, 1e-5F, "ease-out-cubic must start at zero");
    CheckNear(fx::EaseOutCubic(1.0F), 1.0F, 1e-5F, "ease-out-cubic must end at one");
    CheckNear(fx::EaseOutQuint(0.0F), 0.0F, 1e-5F, "ease-out-quint must start at zero");
    CheckNear(fx::EaseOutQuint(1.0F), 1.0F, 1e-5F, "ease-out-quint must end at one");
    CheckNear(fx::EaseInOutCubic(0.0F), 0.0F, 1e-5F, "ease-in-out must start at zero");
    CheckNear(fx::EaseInOutCubic(1.0F), 1.0F, 1e-5F, "ease-in-out must end at one");
    Check(Monotonic(fx::EaseOutCubic), "ease-out-cubic must be monotonic");
    Check(Monotonic(fx::EaseOutQuint), "ease-out-quint must be monotonic");
    Check(Monotonic(fx::EaseInOutCubic), "ease-in-out must be monotonic");
    // Entrances are ease-out, so most of the distance is covered early.
    Check(fx::EaseOutCubic(0.35F) > 0.6F, "ease-out must front-load its travel");
    // The one sanctioned overshoot stays small: a pop, never a slingshot.
    for (int step = 0; step <= 64; ++step) {
        const float value = fx::EaseOutBack(static_cast<float>(step) / 64.0F);
        Check(value >= -0.01F && value <= 1.12F, "ease-out-back must stay bounded");
    }
    Check(fx::Saturate(-2.0F) == 0.0F && fx::Saturate(3.0F) == 1.0F, "saturate must clamp");
    const float period = 2.0F;
    CheckNear(fx::Pulse(0.0F, period), 0.0F, 1e-4F, "pulse must start quiet");
    CheckNear(fx::Pulse(period * 0.5F, period), 1.0F, 1e-4F, "pulse must peak at half period");
    CheckNear(fx::Pulse(period, period), 0.0F, 1e-4F, "pulse must return to quiet");
}

void CheckTransition() {
    fx::Transition transition(0.0F, 16.0F);
    transition.retarget(1.0F);
    // Retargeting must not teleport: the value leaves from where it was, which
    // is what makes rapid hover changes continuous.
    transition.update(0.05F);
    const float early = transition.value();
    Check(early > 0.0F && early < 0.75F, "transition must ease, not jump");
    transition.retarget(0.0F);
    transition.update(0.0F);
    CheckNear(transition.value(), early, 1e-6F, "retarget must preserve the current value");
    for (int step = 0; step < 400; ++step) {
        transition.update(0.016F);
    }
    Check(transition.settled(0.001F), "transition must settle on its target");
    CheckNear(transition.value(), 0.0F, 0.01F, "transition must settle at the retargeted value");

    // Frame-rate independence: one 0.1s step must land near ten 0.01s steps.
    fx::Transition coarse(0.0F, 12.0F);
    fx::Transition fine(0.0F, 12.0F);
    coarse.retarget(1.0F);
    fine.retarget(1.0F);
    coarse.update(0.1F);
    for (int step = 0; step < 10; ++step) {
        fine.update(0.01F);
    }
    CheckNear(coarse.value(), fine.value(), 0.01F, "transition must be frame-rate independent");
    CheckNear(coarse.value(), 1.0F - fx::Exp(-1.2F), 0.01F, "transition must follow the exponential law");
}

void CheckClock() {
    fx::Clock clock;
    Check(!clock.running() && !clock.completed(), "a fresh clock is idle");
    CheckNear(clock.linear(), 0.0F, 1e-6F, "an idle clock reads zero");
    clock.start(0.2F, 0.05F);
    Check(clock.running() && clock.waiting(), "clock must honour its delay");
    clock.update(0.05F);
    Check(!clock.waiting(), "clock must leave the delay window");
    CheckNear(clock.linear(), 0.0F, 1e-4F, "progress starts after the delay");
    CheckNear(clock.decay(), 1.0F, 1e-4F, "an impact flash must start at full strength");
    clock.update(0.1F);
    CheckNear(clock.linear(), 0.5F, 1e-3F, "clock must advance linearly");
    Check(clock.decay() < 0.5F && clock.decay() > 0.0F, "decay must fall from one toward zero");
    clock.update(1.0F);
    Check(clock.completed() && !clock.running(), "clock must complete");
    CheckNear(clock.linear(), 1.0F, 1e-6F, "a completed clock reads one");
    CheckNear(clock.remaining(), 0.0F, 1e-6F, "a completed clock has no time left");
    clock.start(0.3F);
    Check(!clock.completed(), "restarting must clear completion");
    // An entrance may start at nothing, but it must reach full presence.
    CheckNear(clock.eased(), 0.0F, 1e-6F, "restarted clock starts from zero");
    clock.update(0.3F);
    CheckNear(clock.eased(), 1.0F, 1e-6F, "entrance must finish fully visible");
}

void CheckParticles() {
    fx::Rng first(1234U);
    fx::Rng second(1234U);
    for (int index = 0; index < 8; ++index) {
        CheckNear(first.unit(), second.unit(), 1e-7F, "rng must be deterministic");
    }
    fx::Rng rng(7U);
    for (int index = 0; index < 2000; ++index) {
        const float value = rng.unit();
        Check(value >= 0.0F && value < 1.0F, "rng must stay in range");
    }

    fx::Particles particles(32);
    Check(particles.capacity() == 32U, "pool must keep its capacity");
    for (int index = 0; index < 200; ++index) {
        particles.burst({100.0F, 100.0F}, 4, 120.0F, 0.5F, 2.0F, Color{255, 255, 255, 255}, rng);
    }
    Check(particles.live() <= static_cast<int>(particles.capacity()), "pool must never overshoot its budget");
    for (int step = 0; step < 120; ++step) {
        particles.update(0.02F);
    }
    Check(particles.live() == 0, "particles must expire");
    for (int step = 0; step < 10; ++step) {
        particles.update(0.016F);
    }
    particles.spawn({0.0F, 0.0F}, {10.0F, 0.0F}, 0.4F, 3.0F, Color{255, 0, 0, 255});
    particles.update(0.1F);
    Check(particles.live() == 1, "a live particle must survive");
    const auto& particle = particles.items().front();
    Check(particle.x > 0.9F, "particles must integrate velocity");
    Check(particle.life < particle.max_life, "particles must age");
    // A zero life request must not create a permanently immortal particle.
    particles.spawn({0.0F, 0.0F}, {0.0F, 0.0F}, 0.0F, 1.0F, Color{255, 255, 255, 255});
    Check(particles.items().back().life > 0.0F, "spawn must clamp a zero lifetime");
}

void CheckShake() {
    fx::Shake shake;
    Check(!shake.active(), "a fresh shake is calm");
    CheckNear(shake.offset(1.0F).x, 0.0F, 1e-6F, "a calm shake must not offset");
    shake.add(1.0F);
    shake.setLimit(6.0F);
    bool moved = false;
    for (int step = 0; step < 60; ++step) {
        const float time = static_cast<float>(step) * 0.02F;
        const Vector2 offset = shake.offset(time);
        Check(std::abs(offset.x) <= 6.0F && std::abs(offset.y) <= 6.0F, "shake must respect its limit");
        CheckNear(offset.x, std::round(offset.x), 1e-6F, "shake must stay on whole pixels");
        if (std::abs(offset.x) > 0.5F) {
            moved = true;
        }
        shake.update(0.02F);
    }
    Check(moved, "shake must actually move");
    for (int step = 0; step < 120; ++step) {
        shake.update(0.02F);
    }
    Check(!shake.active(), "shake must decay to rest");
    shake.add(0.5F);
    shake.add(0.5F);
    CheckNear(shake.trauma(), 1.0F, 1e-6F, "trauma must saturate");
}

// The board is a fixed 1440x900 canvas: every rectangle must fit inside it and
// the HUD groups must not fight for the same pixels.
void CheckLayout() {
    Check(ui::InsideCanvas({0.0F, 0.0F, ui::kCanvasWidth, ui::kCanvasHeight}), "canvas must contain itself");
    const Rectangle panes[]{ui::CharacterPane(), ui::ConsolePane(), ui::PhasePane(),  ui::StackPane(),
                            ui::ScheduledPane(), ui::ActionPane(),  ui::RestartButton(), ui::Rail()};
    for (const auto& pane : panes) {
        Check(ui::InsideCanvas(pane), "every pane must stay on the canvas");
        Check(pane.width > 0.0F && pane.height > 0.0F, "panes must have area");
    }
    for (int first = 0; first < 8; ++first) {
        for (int second = first + 1; second < 8; ++second) {
            Check(ui::Disjoint(panes[first], panes[second]), "panes must not overlap");
        }
    }

    for (int top = 0; top < 2; ++top) {
        const Rectangle bar = ui::PlayerBar(top == 0);
        Check(ui::InsideCanvas(bar), "player bar must stay on the canvas");
        const Rectangle parts[]{ui::Avatar(bar.y),      ui::PlayerName(bar.y), ui::LifeBar(bar.y),
                                ui::ShieldChip(bar.y),  ui::PlayerStats(bar.y), ui::PriorityStripe(bar.y)};
        for (const auto& part : parts) {
            Check(ui::Contains(bar, part), "player HUD groups must sit inside the bar");
        }
        const Rectangle groups[]{ui::Avatar(bar.y), ui::PlayerName(bar.y), ui::LifeBar(bar.y), ui::ShieldChip(bar.y),
                                 ui::PlayerStats(bar.y), ui::PriorityStripe(bar.y)};
        for (int first = 0; first < 6; ++first) {
            for (int second = first + 1; second < 6; ++second) {
                Check(ui::Disjoint(groups[first], groups[second]), "player HUD groups must not overlap");
            }
        }
        float previous_right = -1.0F;
        for (int pip = 0; pip < ui::kManaPips; ++pip) {
            const Rectangle slot = ui::ManaPip(bar.y, pip);
            Check(ui::Contains(bar, slot), "mana pips must sit inside the bar");
            Check(ui::Disjoint(slot, ui::ShieldChip(bar.y)), "mana pips must clear the shield chip");
            Check(slot.x > previous_right, "mana pips must advance left to right");
            previous_right = slot.x + slot.width;
        }
        CheckNear(previous_right, ui::kBoardX + ui::kBoardWidth - ui::kPlayerPadding, 0.01F,
                  "the mana row must end at the bar's inner edge");
        Check(ui::ManaPipCentreX() > ui::PlayerStats(bar.y).x + 300.0F,
              "the mana readout must sit under its pips");
    }

    const Rectangle rows[]{ui::RowBand(0), ui::RowBand(1), ui::RowBand(2), ui::RowBand(3)};
    for (int index = 0; index < 4; ++index) {
        Check(ui::InsideCanvas(rows[index]), "row bands must stay on the canvas");
        Check(ui::Disjoint(rows[index], ui::PlayerBar(true)), "rows must clear the upper bar");
        Check(ui::Disjoint(rows[index], ui::PlayerBar(false)), "rows must clear the lower bar");
        if (index > 0) {
            Check(rows[index].y > rows[index - 1].y, "row bands must run top to bottom");
            Check(ui::Disjoint(rows[index], rows[index - 1]), "row bands must not overlap");
        }
    }
    // The rail lives in the gap between the two front rows.
    Check(ui::Disjoint(ui::Rail(), ui::RowBand(1)) && ui::Disjoint(ui::Rail(), ui::RowBand(2)),
          "the stack rail must clear both front rows");
    Check(ui::Rail().y >= rows[1].y + rows[1].height, "the rail must sit below the upper front row");
    Check(ui::Rail().y + ui::Rail().height <= rows[2].y, "the rail must sit above the lower front row");
    for (int band = 0; band < 4; ++band) {
        for (int column = 0; column < 3; ++column) {
            const Rectangle slot = ui::Slot(band, column);
            Check(ui::InsideCanvas(slot), "slots must stay on the canvas");
            Check(ui::Contains(ui::RowBand(band), slot), "slots must sit inside their band");
            CheckNear(slot.x, ui::kBoardX + static_cast<float>(column) * ui::kColumnStride, 0.01F,
                      "slot columns must follow the board stride");
            if (column == 2) {
                CheckNear(slot.x + slot.width, ui::kBoardX + ui::kBoardWidth, 0.01F,
                          "the last slot column must end at the board edge");
            }
            if (column > 0) {
                const Rectangle previous = ui::Slot(band, column - 1);
                CheckNear(slot.x - (previous.x + previous.width), ui::kColumnGap, 0.01F,
                          "slot gutters must be even");
                Check(ui::Disjoint(slot, previous), "slot columns must not overlap");
            }
        }
    }

    for (int column = 0; column < 4; ++column) {
        const Rectangle card = ui::HandCard(column);
        Check(ui::InsideCanvas(card), "hand cards must stay on the canvas");
        Check(ui::InsideCanvas(ui::HandCardLifted(column)), "a lifted hand card must stay on the canvas");
        Check(ui::HandCardLifted(column).y > ui::kHandLabelY + 24.0F, "a lifted card must clear the hand label");
        Check(card.y + card.height <= ui::kCanvasHeight, "hand cards must clear the bottom edge");
        if (column > 0) {
            Check(ui::Disjoint(card, ui::HandCard(column - 1)), "hand cards must not overlap");
        }
    }
    Check(ui::HandCard(3).x + ui::HandCard(3).width == ui::kCanvasWidth - 28.0F,
          "the hand row must reach the right margin");
    const Rectangle names[]{ui::PlayerName(ui::kTopPlayerY), ui::PlayerName(ui::kBottomPlayerY)};
    Check(ui::Disjoint(names[0], ui::LifeBar(ui::kTopPlayerY)), "the name must clear the life bar");
    Check(ui::Disjoint(ui::LifeBar(ui::kBottomPlayerY), ui::ShieldChip(ui::kBottomPlayerY)),
          "the life bar must clear the shield chip");
    Check(ui::Disjoint(ui::ShieldChip(ui::kTopPlayerY), ui::ManaPip(ui::kTopPlayerY, 0)),
          "the shield chip must clear the first mana pip");
}

fx::Snapshot BaseSnapshot() {
    fx::Snapshot snapshot;
    snapshot.turn = 3;
    snapshot.phase = 0;
    snapshot.active = 0;
    snapshot.priority = 0;
    for (int index = 0; index < 2; ++index) {
        snapshot.players[index].life = 30;
        snapshot.players[index].max_life = 30;
        snapshot.players[index].mana = 3;
        snapshot.players[index].max_mana = 4;
        snapshot.players[index].shield = 0;
    }
    fx::UnitSnapshot unit;
    unit.id = 7;
    unit.life = 4;
    unit.max_life = 5;
    unit.shield = 0;
    snapshot.units[1].push_back(unit);
    return snapshot;
}

void CheckDirector() {
    fx::Director director(4242U);
    const Rectangle bar{284.0F, 579.0F, 848.0F, 81.0F};
    director.placePlayer(0, bar, Color{244, 170, 197, 255});
    director.placePlayer(1, bar, Color{148, 215, 220, 255});
    director.placeUnit(7, {284.0F, 386.0F, 276.0F, 84.0F}, "unit", Color{148, 215, 220, 255});
    director.placeStack({1150.0F, 299.0F, 262.0F, 178.0F});
    director.placeRail({284.0F, 364.0F, 848.0F, 21.0F});

    // First observation establishes the baseline: no effects for existing state.
    auto snapshot = BaseSnapshot();
    director.observe(snapshot);
    Check(director.floaterList().empty() && director.ringList().empty() && director.beamList().empty() &&
              director.ghostList().empty(),
          "the first observation must not fire effects");
    CheckNear(director.unitEnter(7), 1.0F, 1e-6F, "pre-existing units are already on stage");
    director.advance(0.016F);

    // Life loss: floater, shake, flash, and a bar that eases behind the number.
    snapshot.players[0].life = 24;
    director.observe(snapshot);
    Check(director.effectCount() > 0U, "damage must spawn feedback");
    Check(director.flashAlpha() > 0.0F, "hero damage must flash");
    const float lagging = director.playerLife(0, 0.8F);
    Check(lagging > 0.8F && lagging <= 1.0F, "the life bar must lag behind the number");
    bool moved = false;
    for (int step = 0; step < 30; ++step) {
        director.advance(0.016F);
        const Vector2 offset = director.shake();
        if (std::abs(offset.x) > 0.5F || std::abs(offset.y) > 0.5F) {
            moved = true;
        }
    }
    Check(moved, "hero damage must shake the stage");
    for (int step = 0; step < 60; ++step) {
        director.advance(0.016F);
    }
    CheckNear(director.playerLife(0, 0.8F), 0.8F, 0.01F, "the life bar must catch up");
    Check(director.shake().x == 0.0F && director.shake().y == 0.0F, "shake must settle");

    // Unit damage and death.
    snapshot.units[1][0].life = 2;
    director.observe(snapshot);
    Check(director.unitHit(7) > 0.0F, "unit damage must flash the card");
    Check(director.unitLife(7, 0.4F) > 0.4F, "unit health must ease down");
    snapshot.units[1].clear();
    director.observe(snapshot);
    Check(director.ghostList().size() == 1U, "a defeated unit must leave a dissolving ghost");
    for (int step = 0; step < 80; ++step) {
        director.advance(0.016F);
    }
    Check(director.ghostList().empty(), "ghosts must clear after their dissolve");

    // A new unit enters instead of appearing: scale-free rise and fade.
    snapshot.units[1].push_back(fx::UnitSnapshot{11, 5, 5, 0, false});
    director.observe(snapshot);
    director.placeUnit(11, {856.0F, 386.0F, 276.0F, 84.0F}, "fresh", Color{244, 170, 197, 255});
    director.advance(0.001F);
    Check(director.unitEnter(11) < 1.0F, "a summoned unit must animate in");
    Check(director.unitEnter(11) > 0.0F, "an entering unit must be visible, not scaled from nothing");
    for (int step = 0; step < 40; ++step) {
        director.advance(0.016F);
    }
    CheckNear(director.unitEnter(11), 1.0F, 0.02F, "an entrance must complete");

    // Stack pushes pull a line from the source to the target.
    snapshot.stack_size = 1;
    snapshot.top.present = true;
    snapshot.top.controller = 1;
    snapshot.top.attacker = 11;
    snapshot.top.target_player = 0;
    director.observe(snapshot);
    Check(!director.beamList().empty(), "a stack push must draw a resolution line");
    director.advance(0.05F);
    Check(director.stackArrival() > 0.0F, "a stack push must pulse the stack panel");

    // Mana gain and the finished celebration.
    snapshot.players[1].mana = 4;
    director.observe(snapshot);
    for (int step = 0; step < 14; ++step) {
        director.advance(0.016F);
    }
    Check(director.playerMana(1, 1.0F) > 0.9F, "mana pips must ease up to the new total");
    snapshot.finished = true;
    snapshot.winner = 0;
    director.observe(snapshot);
    Check(director.celebration() >= 0.0F, "the result must drive a reveal");
    Check(director.effectCount() > 0U, "winning must celebrate");

    // Reduced motion keeps the state changes and drops movement.
    fx::Director calm(99U);
    calm.settings().motion = false;
    calm.settings().particles = false;
    calm.settings().shake = false;
    calm.placePlayer(0, bar, Color{244, 170, 197, 255});
    auto calm_snapshot = BaseSnapshot();
    calm.observe(calm_snapshot);
    calm_snapshot.players[0].life = 10;
    calm.observe(calm_snapshot);
    Check(calm.shake().x == 0.0F && calm.shake().y == 0.0F, "reduced motion must not shake");
    Check(calm.flashAlpha() == 0.0F, "reduced motion must not flash the screen");
    Check(calm.playerLife(0, 0.33F) >= 0.0F, "reduced motion must keep the state readable");

    fx::Director reset(5U);
    reset.placePlayer(0, bar, Color{255, 255, 255, 255});
    reset.observe(BaseSnapshot());
    reset.reset();
    Check(reset.effectCount() == 0U, "reset must clear every effect");
    CheckNear(reset.playerLife(0, 0.5F), 1.0F, 1e-6F, "reset must restore the transitions");
}

void CheckEffectBudget() {
    // A long duel with heavy feedback must stay inside the pool and finish.
    fx::Director director(20261005U);
    director.placePlayer(0, {284.0F, 579.0F, 848.0F, 81.0F}, Color{244, 170, 197, 255});
    director.placePlayer(1, {284.0F, 86.0F, 848.0F, 81.0F}, Color{148, 215, 220, 255});
    auto snapshot = BaseSnapshot();
    director.observe(snapshot);
    for (int frame = 0; frame < 400; ++frame) {
        snapshot.players[0].life = 30 - (frame % 20);
        snapshot.players[1].life = 30 - (frame % 13);
        snapshot.units[1].clear();
        if (frame % 7 != 0) {
            fx::UnitSnapshot unit;
            unit.id = static_cast<std::uint64_t>(frame) + 1U;
            unit.life = 1 + frame % 4;
            unit.max_life = 5;
            snapshot.units[1].push_back(unit);
            director.placeUnit(unit.id, {284.0F, 386.0F, 276.0F, 84.0F}, "unit", Color{255, 255, 255, 255});
        }
        snapshot.stack_size = frame % 4;
        director.observe(snapshot);
        director.advance(0.016F);
        Check(director.particleList().size() <= 640U, "the particle pool must stay capped");
        Check(director.ghostList().size() <= 8U, "the ghost list must stay capped");
    }
    Check(director.time() > 0.0F, "the director must advance its clock");
}

void CheckReveal() {
    CheckNear(fx::Reveal(0.0F, 0), 0.0F, 1e-6F, "a reveal must start hidden");
    CheckNear(fx::Reveal(1.0F, 0), 1.0F, 1e-6F, "a reveal must finish visible");
    CheckNear(fx::Reveal(0.04F, 0), 0.2F, 1e-3F, "a reveal must fade over its duration");
    // Rows arrive in sequence, never all at once.
    Check(fx::Reveal(0.1F, 0) > fx::Reveal(0.1F, 3), "later rows must wait their turn");
    CheckNear(fx::Reveal(0.02F, 5), 0.0F, 1e-6F, "a delayed row must still be hidden");
}
}  // namespace

int main() {
    try {
        CheckEasing();
        CheckTransition();
        CheckClock();
        CheckParticles();
        CheckShake();
        CheckLayout();
        CheckDirector();
        CheckEffectBudget();
        CheckReveal();
        std::cout << "All Cardis client motion and layout checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
