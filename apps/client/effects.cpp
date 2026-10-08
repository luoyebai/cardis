#include "effects.hpp"

#include <algorithm>
#include <cmath>

namespace cardis::fx {
namespace {
constexpr float kTau = 6.2831855F;
constexpr std::size_t kMaxGhosts = 8;
constexpr int kMaxAmbient = 46;

// Layered sine pair: cheap, deterministic, and smooth enough for shake and
// drift without a noise table.
float Wobble(float value, float offset) {
    return 0.62F * Sin(value + offset) + 0.38F * Sin(value * 2.17F + offset * 1.7F + 1.3F);
}
float Fraction(int value, int maximum) {
    return maximum > 0 ? static_cast<float>(value) / static_cast<float>(maximum) : 0.0F;
}
}  // namespace

float Saturate(float value) { return std::clamp(value, 0.0F, 1.0F); }
float Sin(float radians) { return static_cast<float>(std::sin(static_cast<double>(radians))); }
float Cos(float radians) { return static_cast<float>(std::cos(static_cast<double>(radians))); }
float Sqrt(float value) { return static_cast<float>(std::sqrt(static_cast<double>(std::max(0.0F, value)))); }
float Pow(float base, float exponent) {
    return static_cast<float>(std::pow(static_cast<double>(base), static_cast<double>(exponent)));
}
float Exp(float value) { return static_cast<float>(std::exp(static_cast<double>(value))); }
float Floor(float value) { return static_cast<float>(std::floor(static_cast<double>(value))); }
float Ceil(float value) { return static_cast<float>(std::ceil(static_cast<double>(value))); }
float Round(float value) { return static_cast<float>(std::round(static_cast<double>(value))); }

float EaseOutCubic(float t) {
    const float x = 1.0F - Saturate(t);
    return 1.0F - x * x * x;
}
float EaseOutQuint(float t) {
    const float x = 1.0F - Saturate(t);
    return 1.0F - x * x * x * x * x;
}
float EaseInOutCubic(float t) {
    const float x = Saturate(t);
    return x < 0.5F ? 4.0F * x * x * x : 1.0F - Pow(-2.0F * x + 2.0F, 3.0F) / 2.0F;
}
float EaseOutBack(float t) {
    const float x = Saturate(t) - 1.0F;
    return 1.0F + 2.2F * x * x * x + 1.2F * x * x;
}
float Damp(float current, float target, float sharpness, float delta) {
    const float factor = 1.0F - Exp(-std::max(0.0F, sharpness) * std::max(0.0F, delta));
    return current + (target - current) * factor;
}
float Pulse(float time, float period) {
    if (period <= 0.0F) {
        return 0.0F;
    }
    return 0.5F - 0.5F * Cos(kTau * time / period);
}
float Reveal(float elapsed, int index, float step, float duration) {
    const float delayed = elapsed - static_cast<float>(index) * step;
    return duration <= 0.0F ? (delayed > 0.0F ? 1.0F : 0.0F) : Saturate(delayed / duration);
}

Rng::Rng(std::uint32_t seed) : state_(seed == 0 ? 0x9E3779B9U : seed) {}
float Rng::unit() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<float>(state_ >> 8) / 16777216.0F;
}
float Rng::range(float low, float high) { return low + (high - low) * unit(); }
float Rng::spread(float magnitude) { return range(-magnitude, magnitude); }
int Rng::below(int count) {
    return count <= 1 ? 0 : static_cast<int>(unit() * static_cast<float>(count)) % count;
}

Transition::Transition(float value, float sharpness) : value_(value), target_(value), sharpness_(sharpness) {}
void Transition::snap(float value) {
    value_ = value;
    target_ = value;
}
void Transition::retarget(float target) { target_ = target; }
void Transition::update(float delta) { value_ = Damp(value_, target_, sharpness_, delta); }
float Transition::value() const { return value_; }
float Transition::target() const { return target_; }
bool Transition::settled(float epsilon) const { return std::abs(target_ - value_) <= epsilon; }
void Transition::setSharpness(float sharpness) { sharpness_ = sharpness; }

void Clock::start(float duration, float delay) {
    elapsed_ = 0;
    duration_ = std::max(0.0001F, duration);
    delay_ = std::max(0.0F, delay);
    running_ = true;
    completed_ = false;
}
void Clock::update(float delta) {
    if (!running_) {
        return;
    }
    elapsed_ += std::max(0.0F, delta);
    if (elapsed_ >= duration_ + delay_) {
        elapsed_ = duration_ + delay_;
        running_ = false;
        completed_ = true;
    }
}
void Clock::finish() {
    elapsed_ = duration_ + delay_;
    running_ = false;
    completed_ = true;
}
bool Clock::running() const { return running_; }
bool Clock::waiting() const { return running_ && elapsed_ < delay_; }
bool Clock::completed() const { return completed_; }
float Clock::elapsed() const { return elapsed_; }
float Clock::duration() const { return duration_; }
float Clock::remaining() const { return std::max(0.0F, duration_ + delay_ - elapsed_); }
float Clock::linear() const {
    if (completed_) {
        return 1.0F;
    }
    const float active = elapsed_ - delay_;
    return active <= 0.0F ? 0.0F : Saturate(active / duration_);
}
float Clock::eased() const { return EaseOutCubic(linear()); }
float Clock::decay() const { return 1.0F - EaseOutCubic(linear()); }

float EffectSettings::scale(float value) const { return motion ? value * intensity : 0.0F; }

Particles::Particles(int capacity) : capacity_(static_cast<std::size_t>(std::max(1, capacity))) {
    items_.reserve(capacity_);
}
void Particles::clear() { items_.clear(); }
void Particles::spawn(Vector2 position, Vector2 velocity, float life, float size, Color color, float gravity,
                      float drag) {
    Particle particle;
    particle.x = position.x;
    particle.y = position.y;
    particle.velocity_x = velocity.x;
    particle.velocity_y = velocity.y;
    particle.life = life;
    particle.size = size;
    particle.gravity = gravity;
    particle.drag = drag;
    particle.color = color;
    spawn(particle);
}
void Particles::spawn(ParticleKind kind, Vector2 position, Vector2 velocity, float life, float size, Color color) {
    Particle particle;
    particle.kind = kind;
    particle.x = position.x;
    particle.y = position.y;
    particle.velocity_x = velocity.x;
    particle.velocity_y = velocity.y;
    particle.life = life;
    particle.size = size;
    particle.color = color;
    spawn(particle);
}
void Particles::spawn(const Particle& particle) {
    if (items_.size() >= capacity_) {
        return;
    }
    Particle entry = particle;
    entry.life = std::max(0.01F, entry.life);
    entry.max_life = entry.life;
    entry.size = std::max(0.5F, entry.size);
    items_.push_back(entry);
}
void Particles::burst(Vector2 origin, int count, float speed, float life, float size, Color color, Rng& rng, float angle,
                      float spread) {
    for (int index = 0; index < count; ++index) {
        const float direction = angle + rng.spread(spread * 0.5F);
        const float magnitude = speed * rng.range(0.45F, 1.0F);
        spawn(origin, {Cos(direction) * magnitude, Sin(direction) * magnitude}, life * rng.range(0.65F, 1.15F),
              size * rng.range(0.7F, 1.3F), color);
    }
}
void Particles::update(float delta) {
    const float step = std::max(0.0F, delta);
    std::size_t write = 0;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        Particle particle = items_[index];
        particle.life -= step;
        if (particle.life <= 0.0F) {
            continue;
        }
        if (particle.drag > 0.0F) {
            const float keep = Exp(-particle.drag * step);
            particle.velocity_x *= keep;
            particle.velocity_y *= keep;
        }
        particle.velocity_y += particle.gravity * step;
        particle.x += particle.velocity_x * step;
        particle.y += particle.velocity_y * step;
        particle.rotation += particle.spin * step;
        items_[write++] = particle;
    }
    items_.resize(write);
}
const std::vector<Particle>& Particles::items() const { return items_; }
int Particles::live() const { return static_cast<int>(items_.size()); }
std::size_t Particles::capacity() const { return capacity_; }

void Shake::add(float trauma) { trauma_ = Saturate(trauma_ + trauma); }
void Shake::update(float delta) {
    trauma_ = std::max(0.0F, trauma_ - std::max(0.0F, delta) * 1.7F);
}
void Shake::clear() { trauma_ = 0.0F; }
void Shake::setLimit(float pixels) { limit_ = std::max(0.0F, pixels); }
Vector2 Shake::offset(float time) const {
    if (trauma_ <= 0.0F) {
        return {0, 0};
    }
    const float amplitude = trauma_ * trauma_ * limit_;
    return {Round(amplitude * Wobble(time * 39.0F, 0.0F)), Round(amplitude * Wobble(time * 31.0F, 5.7F))};
}
float Shake::trauma() const { return trauma_; }
bool Shake::active() const { return trauma_ > 0.0F; }

Director::Director(std::uint32_t seed) : particles_(640), rng_(seed) {}

void Director::reset() {
    units_.clear();
    ghosts_.clear();
    players_[0] = PlayerFx{};
    players_[1] = PlayerFx{};
    particles_.clear();
    rings_.clear();
    beams_.clear();
    floaters_.clear();
    shake_.clear();
    flash_alpha_ = 0;
    ambient_timer_ = 0;
    time_ = 0;
    observed_ = false;
    turn_ = 0;
    stack_size_ = 0;
    finished_ = false;
    for (int index = 0; index < 2; ++index) {
        previous_life_[index] = -1;
        previous_shield_[index] = 0;
        previous_mana_[index] = -1;
    }
}
EffectSettings& Director::settings() { return settings_; }
const EffectSettings& Director::settings() const { return settings_; }
float Director::time() const { return time_; }

void Director::advance(float delta) {
    const float step = std::clamp(delta, 0.0F, 0.1F);
    time_ += step;
    shake_.update(step);
    flash_alpha_ = std::max(0.0F, flash_alpha_ - step * 4.2F);
    particles_.update(step);
    for (auto& unit : units_) {
        unit.enter.update(step);
        unit.hit.update(step);
        unit.guard.update(step);
        unit.life.update(step);
    }
    for (auto& player : players_) {
        player.hit.update(step);
        player.life.update(step);
        player.mana.update(step);
    }
    stack_arrival_.update(step);
    turn_arrival_.update(step);
    celebration_.update(step);
    for (auto& ring : rings_) {
        ring.life -= step;
        ring.radius += ring.growth * step;
    }
    rings_.erase(std::remove_if(rings_.begin(), rings_.end(), [](const Ring& ring) { return ring.life <= 0.0F; }),
                 rings_.end());
    for (auto& beam : beams_) {
        beam.life -= step;
    }
    beams_.erase(std::remove_if(beams_.begin(), beams_.end(), [](const Beam& beam) { return beam.life <= 0.0F; }),
                 beams_.end());
    for (auto& floater : floaters_) {
        floater.life -= step;
        if (!settings_.motion) {
            continue;  // reduced motion: the number fades in place
        }
        const float progress = 1.0F - Saturate(floater.life / floater.max_life);
        floater.y -= floater.rise * step * (1.0F - EaseOutCubic(progress)) * 2.6F;
        floater.x += floater.drift * step * (1.0F - progress);
    }
    floaters_.erase(std::remove_if(floaters_.begin(), floaters_.end(),
                                   [](const Floater& floater) { return floater.life <= 0.0F; }),
                    floaters_.end());
    for (auto& ghost : ghosts_) {
        ghost.life -= step;
    }
    ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), [](const Ghost& ghost) { return ghost.life <= 0.0F; }),
                  ghosts_.end());
    spawnAmbient(step);
}

void Director::spawnAmbient(float delta) {
    if (!settings_.motion || !settings_.particles || particles_.live() >= kMaxAmbient) {
        return;
    }
    ambient_timer_ -= delta;
    if (ambient_timer_ > 0.0F) {
        return;
    }
    ambient_timer_ = rng_.range(0.14F, 0.34F);
    const Vector2 origin{rng_.range(300.0F, 1120.0F), rng_.range(120.0F, 880.0F)};
    const Color tint = rng_.unit() < 0.5F ? Color{244, 170, 197, 255} : Color{148, 215, 220, 255};
    particles_.spawn(ParticleKind::AMBIENT, origin, {rng_.spread(4.0F), rng_.range(-14.0F, -6.0F)},
                     rng_.range(3.4F, 6.5F), rng_.range(1.0F, 2.1F), tint);
}

Director::UnitFx* Director::findUnit(std::uint64_t id) const {
    for (auto& unit : units_) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

void Director::spawnImpact(const UnitFx& unit, int damage, int shield_gain, int life_after) {
    const Vector2 centre{unit.bounds.x + unit.bounds.width * 0.5F, unit.bounds.y + unit.bounds.height * 0.5F};
    if (damage > 0) {
        particles_.burst(centre, static_cast<int>(settings_.scale(14.0F)), 190.0F, 0.42F, 2.6F,
                         Color{255, 214, 226, 255}, rng_);
        rings_.push_back(Ring{centre.x, centre.y, 6.0F, settings_.scale(120.0F), 0.34F, 0.34F, 2.0F,
                              Color{255, 150, 180, 255}});
        floaters_.push_back(Floater{centre.x - 10.0F, unit.bounds.y + 6.0F, dur::kFloater, dur::kFloater, 34.0F, 0.0F,
                                    Color{255, 168, 190, 255}, "-" + std::to_string(damage)});
        if (life_after <= 0) {
            floaters_.push_back(Floater{centre.x + 22.0F, unit.bounds.y + 22.0F, dur::kFloater, dur::kFloater, 30.0F,
                                        0.0F, Color{246, 229, 192, 255}, "退场"});
        }
    }
    if (shield_gain > 0) {
        particles_.burst(centre, static_cast<int>(settings_.scale(9.0F)), 90.0F, 0.5F, 2.2F, Color{246, 229, 192, 255},
                         rng_);
        floaters_.push_back(Floater{centre.x - 14.0F, unit.bounds.y - 4.0F, dur::kFloater, dur::kFloater, 26.0F, 0.0F,
                                    Color{246, 229, 192, 255}, "护 +" + std::to_string(shield_gain)});
    }
}

void Director::spawnPlayerDamage(int index, int damage, int life_after) {
    const auto& fx = players_[index];
    if (!fx.placed) {
        return;
    }
    const Vector2 centre{fx.bounds.x + fx.bounds.width * 0.5F, fx.bounds.y + fx.bounds.height * 0.5F};
    const Color accent = fx.accent;
    particles_.burst(centre, static_cast<int>(settings_.scale(20.0F)), 260.0F, 0.5F, 3.0F, accent, rng_);
    rings_.push_back(Ring{centre.x, centre.y, 10.0F, settings_.scale(220.0F), 0.42F, 0.42F, 2.5F, accent});
    floaters_.push_back(Floater{fx.bounds.x + fx.bounds.width * 0.5F - 26.0F, fx.bounds.y + 12.0F, dur::kFloater,
                                dur::kFloater, 40.0F, 0.0F, Color{255, 150, 170, 255},
                                "-" + std::to_string(damage)});
    if (life_after <= 0) {
        floaters_.push_back(Floater{fx.bounds.x + fx.bounds.width * 0.5F - 20.0F, fx.bounds.y + 46.0F, dur::kFloater,
                                    dur::kFloater, 30.0F, 0.0F, Color{246, 229, 192, 255}, "生命归零"});
    }
    if (settings_.shake && !shake_.active()) {
        shake_.add(Saturate(0.28F + static_cast<float>(damage) * 0.05F));
    }
    flash_color_ = accent;
    flash_alpha_ = settings_.scale(0.16F);
}

void Director::spawnStackEffect(const Snapshot& snapshot) {
    if (!snapshot.top.present) {
        return;
    }
    const int controller = std::clamp(snapshot.top.controller, 0, 1);
    const int target_player = std::clamp(snapshot.top.target_player, 0, 1);
    const Color accent =
        players_[controller].placed ? players_[controller].accent : Color{246, 240, 246, 255};
    Vector2 from{stack_bounds_.x + stack_bounds_.width * 0.5F, stack_bounds_.y + stack_bounds_.height * 0.5F};
    if (const auto* attacker = findUnit(snapshot.top.attacker); attacker != nullptr && attacker->placed) {
        from = {attacker->bounds.x + attacker->bounds.width * 0.5F, attacker->bounds.y + attacker->bounds.height * 0.5F};
    } else if (players_[controller].placed) {
        const auto& bounds = players_[controller].bounds;
        from = {bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
    }
    Vector2 to = from;
    if (const auto* target = findUnit(snapshot.top.target_unit); target != nullptr && target->placed) {
        to = {target->bounds.x + target->bounds.width * 0.5F, target->bounds.y + target->bounds.height * 0.5F};
    } else if (players_[target_player].placed) {
        const auto& bounds = players_[target_player].bounds;
        to = {bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
    }
    beams_.push_back(Beam{from.x, from.y, to.x, to.y, 6.0F, 0.34F, 0.34F, accent});
    rings_.push_back(Ring{to.x, to.y, 8.0F, settings_.scale(150.0F), 0.36F, 0.36F, 2.0F, accent});
    stack_arrival_.start(0.4F);
}

void Director::observe(const Snapshot& snapshot) {
    const bool first = !observed_;
    observed_ = true;

    for (int index = 0; index < 2; ++index) {
        const auto& player = snapshot.players[index];
        auto& fx = players_[index];
        fx.life.retarget(Fraction(player.life, player.max_life));
        fx.mana.retarget(Fraction(player.mana, player.max_mana));
        if (previous_life_[index] >= 0 && !first) {
            const int lost = previous_life_[index] - player.life;
            const int gained = player.life - previous_life_[index];
            const int guarded = player.shield - previous_shield_[index];
            if (lost > 0) {
                spawnPlayerDamage(index, lost, player.life);
            } else if (gained > 0 && fx.placed) {
                floaters_.push_back(Floater{fx.bounds.x + fx.bounds.width * 0.5F - 26.0F, fx.bounds.y + 12.0F,
                                            dur::kFloater, dur::kFloater, 34.0F, 0.0F, Color{159, 230, 176, 255},
                                            "+" + std::to_string(gained)});
                fx.hit.start(0.3F);
            }
            if (guarded > 0 && fx.placed) {
                floaters_.push_back(Floater{fx.bounds.x + fx.bounds.width * 0.5F + 6.0F, fx.bounds.y + 40.0F,
                                            dur::kFloater, dur::kFloater, 26.0F, 0.0F, Color{246, 229, 192, 255},
                                            "护 +" + std::to_string(guarded)});
                particles_.burst({fx.bounds.x + fx.bounds.width * 0.5F, fx.bounds.y + fx.bounds.height * 0.5F},
                                 static_cast<int>(settings_.scale(10.0F)), 110.0F, 0.55F, 2.4F,
                                 Color{246, 229, 192, 255}, rng_);
            }
            if (previous_mana_[index] >= 0 && player.mana > previous_mana_[index] && fx.placed) {
                const Rectangle pips = fx.bounds;
                rings_.push_back(Ring{pips.x + pips.width - 40.0F, pips.y + pips.height * 0.5F, 4.0F,
                                      settings_.scale(70.0F), 0.4F, 0.4F, 1.6F, Color{148, 215, 220, 255}});
            }
        }
        previous_life_[index] = player.life;
        previous_shield_[index] = player.shield;
        previous_mana_[index] = player.mana;
    }

    // Creation and mutation run in two passes: growing the vector would
    // invalidate any pointer held across the first loop.
    for (int owner = 0; owner < 2; ++owner) {
        for (const auto& entry : snapshot.units[owner]) {
            if (findUnit(entry.id) == nullptr) {
                UnitFx record;
                record.id = entry.id;
                units_.push_back(record);
            }
        }
    }
    for (auto& unit : units_) {
        unit.alive = false;
    }
    for (int owner = 0; owner < 2; ++owner) {
        for (const auto& entry : snapshot.units[owner]) {
            auto* record = findUnit(entry.id);
            if (record == nullptr) {
                continue;
            }
            if (!record->established) {
                record->established = true;
                if (first) {
                    record->enter.finish();
                } else {
                    record->enter.start(dur::kEnter);
                    record->pending_summon = true;
                }
                record->life.snap(Fraction(entry.life, entry.max_life));
            } else {
                const int damage = record->last_life - entry.life;
                const int guard = entry.shield - record->last_shield;
                if (damage > 0 || guard > 0) {
                    record->hit.start(dur::kImpact);
                    spawnImpact(*record, damage, guard, entry.life);
                }
                if (guard > 0) {
                    record->guard.start(0.5F);
                }
            }
            record->life.retarget(Fraction(entry.life, entry.max_life));
            record->last_life = entry.life;
            record->last_shield = entry.shield;
            record->alive = true;
        }
    }
    for (auto it = units_.begin(); it != units_.end();) {
        if (it->alive) {
            ++it;
            continue;
        }
        if (it->established && it->placed && ghosts_.size() < kMaxGhosts) {
            ghosts_.push_back(Ghost{it->bounds, it->label, it->accent, dur::kDeath, dur::kDeath, rng_.unit()});
            const Vector2 centre{it->bounds.x + it->bounds.width * 0.5F, it->bounds.y + it->bounds.height * 0.5F};
            particles_.burst(centre, static_cast<int>(settings_.scale(18.0F)), 150.0F, 0.7F, 2.8F,
                             Color{120, 110, 150, 255}, rng_);
        }
        it = units_.erase(it);
    }

    if (!first) {
        if (snapshot.stack_size > stack_size_) {
            spawnStackEffect(snapshot);
        }
        if (snapshot.turn != turn_) {
            turn_arrival_.start(0.9F);
            if (players_[snapshot.active].placed) {
                const auto& bounds = players_[snapshot.active].bounds;
                rings_.push_back(Ring{bounds.x + 74.0F, bounds.y + bounds.height * 0.5F, 8.0F,
                                      settings_.scale(120.0F), 0.7F, 0.7F, 2.0F, players_[snapshot.active].accent});
            }
        }
        if (snapshot.finished && !finished_) {
            celebration_.start(2.2F);
            const Color palette[3]{Color{244, 170, 197, 255}, Color{148, 215, 220, 255}, Color{246, 229, 192, 255}};
            const int confetti = static_cast<int>(settings_.scale(120.0F));
            for (int index = 0; index < confetti; ++index) {
                Particle piece;
                piece.kind = ParticleKind::CONFETTI;
                piece.x = rng_.range(240.0F, 1200.0F);
                piece.y = rng_.range(-60.0F, 20.0F);
                piece.velocity_x = rng_.spread(90.0F);
                piece.velocity_y = rng_.range(60.0F, 210.0F);
                piece.life = rng_.range(1.6F, 3.0F);
                piece.size = rng_.range(4.0F, 8.0F);
                piece.rotation = rng_.range(0.0F, 360.0F);
                piece.spin = rng_.spread(220.0F);
                piece.gravity = 90.0F;
                piece.drag = 0.5F;
                piece.color = palette[rng_.below(3)];
                particles_.spawn(piece);
            }
        }
    }

    turn_ = snapshot.turn;
    stack_size_ = snapshot.stack_size;
    finished_ = snapshot.finished;
}

void Director::placePlayer(int index, Rectangle bounds, Color accent) const {
    if (index < 0 || index > 1) {
        return;
    }
    players_[index].bounds = bounds;
    players_[index].accent = accent;
    players_[index].placed = true;
}
void Director::placeUnit(std::uint64_t id, Rectangle bounds, std::string label, Color accent) const {
    auto* record = findUnit(id);
    if (record == nullptr) {
        UnitFx fresh;
        fresh.id = id;
        fresh.established = true;
        fresh.enter.finish();
        units_.push_back(fresh);
        record = &units_.back();
    }
    record->bounds = bounds;
    record->label = std::move(label);
    record->accent = accent;
    record->placed = true;
    if (record->pending_summon) {
        record->pending_summon = false;
        const Vector2 centre{bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
        rings_.push_back(Ring{centre.x, centre.y, 6.0F, settings_.scale(150.0F), 0.42F, 0.42F, 2.2F, accent});
        particles_.burst(centre, static_cast<int>(settings_.scale(12.0F)), 120.0F, 0.5F, 2.4F, accent, rng_);
    }
}
void Director::placeStack(Rectangle bounds) const { stack_bounds_ = bounds; }
void Director::placeRail(Rectangle bounds) const { rail_bounds_ = bounds; }

float Director::unitEnter(std::uint64_t id) const {
    const auto* record = findUnit(id);
    if (record == nullptr || record->enter.completed()) {
        return 1.0F;
    }
    return record->enter.eased();
}
float Director::unitHit(std::uint64_t id) const {
    const auto* record = findUnit(id);
    return record != nullptr && record->hit.running() ? record->hit.decay() : 0.0F;
}
float Director::unitShield(std::uint64_t id) const {
    const auto* record = findUnit(id);
    return record != nullptr && record->guard.running() ? record->guard.decay() : 0.0F;
}
float Director::unitLife(std::uint64_t id, float target) const {
    const auto* record = findUnit(id);
    if (record == nullptr) {
        return target;
    }
    return record->life.value();
}
float Director::playerHit(int index) const {
    if (index < 0 || index > 1) {
        return 0.0F;
    }
    return players_[index].hit.running() ? players_[index].hit.decay() : 0.0F;
}
float Director::playerLife(int index, float target) const {
    if (index < 0 || index > 1) {
        return target;
    }
    return players_[index].life.value();
}
float Director::playerMana(int index, float target) const {
    if (index < 0 || index > 1) {
        return target;
    }
    return players_[index].mana.value();
}
float Director::stackArrival() const { return stack_arrival_.eased(); }
float Director::turnArrival() const { return turn_arrival_.eased(); }
Vector2 Director::shake() const { return settings_.shake ? shake_.offset(time_) : Vector2{0, 0}; }
float Director::flashAlpha() const { return settings_.motion ? flash_alpha_ : 0.0F; }
Color Director::flashColor() const { return flash_color_; }
float Director::celebration() const { return celebration_.linear(); }
unsigned int Director::effectCount() const {
    return static_cast<unsigned int>(particles_.live() + rings_.size() + beams_.size() + floaters_.size() +
                                     ghosts_.size());
}
const std::vector<Particle>& Director::particleList() const { return particles_.items(); }
const std::vector<Ring>& Director::ringList() const { return rings_; }
const std::vector<Beam>& Director::beamList() const { return beams_; }
const std::vector<Floater>& Director::floaterList() const { return floaters_; }
const std::vector<Ghost>& Director::ghostList() const { return ghosts_; }

}  // namespace cardis::fx
