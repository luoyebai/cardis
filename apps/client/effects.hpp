#pragma once
// Presentation-side motion toolkit for the Cardis client.
//
// Scope: easing curves, retargetable transitions, one-shot clocks, particles,
// floating combat text, impact rings and beams, screen shake and colour
// flashes. The toolkit owns state and math only -- it never calls a raylib
// drawing function -- so tests can exercise every curve, timer and pool
// without a graphics context. Drawing lives in the client.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <raylib.h>

namespace cardis::fx {

// Durations follow the interface motion budget: feedback is near-instant,
// entrances are short, and nothing interactive runs past 300ms.
namespace dur {
inline constexpr float kPress = 0.12F;
inline constexpr float kHover = 0.14F;
inline constexpr float kEnter = 0.24F;
inline constexpr float kModal = 0.20F;
inline constexpr float kFloater = 0.95F;
inline constexpr float kImpact = 0.42F;
inline constexpr float kDeath = 0.48F;
inline constexpr float kStagger = 0.06F;
}  // namespace dur

// ------------------------------------------------------------------ math ---

[[nodiscard]] float Saturate(float value);
[[nodiscard]] float Sin(float radians);
[[nodiscard]] float Cos(float radians);
[[nodiscard]] float Sqrt(float value);
[[nodiscard]] float Pow(float base, float exponent);
[[nodiscard]] float Exp(float value);
[[nodiscard]] float Floor(float value);
[[nodiscard]] float Ceil(float value);
[[nodiscard]] float Round(float value);

// Easing set. Entrances use ease-out, on-screen movement uses ease-in-out.
[[nodiscard]] float EaseOutCubic(float t);
[[nodiscard]] float EaseOutQuint(float t);
[[nodiscard]] float EaseInOutCubic(float t);
[[nodiscard]] float EaseOutBack(float t);
// Frame-rate independent exponential approach: the discrete form of a CSS
// transition, so retargeting mid-flight keeps the current value.
[[nodiscard]] float Damp(float current, float target, float sharpness, float delta);
// Smooth 0 -> 1 -> 0 over one period.
[[nodiscard]] float Pulse(float time, float period);
// Staggered reveal for overlay rows: the row index delays its fade.
[[nodiscard]] float Reveal(float elapsed, int index, float step = dur::kStagger, float duration = dur::kModal);

// Deterministic xorshift32 source so smoke screenshots stay reproducible.
class Rng {
 public:
    explicit Rng(std::uint32_t seed = 0x9E3779B9U);
    [[nodiscard]] float unit();  // [0, 1)
    [[nodiscard]] float range(float low, float high);
    [[nodiscard]] float spread(float magnitude);  // [-magnitude, magnitude]
    [[nodiscard]] int below(int count);

 private:
    std::uint32_t state_;
};

// ------------------------------------------------------------ primitives ---

// Retargetable transition. Retargeting mid-flight never jumps: the value keeps
// moving from where it is, which is what makes rapid hover/selection changes
// feel continuous instead of restarted.
class Transition {
 public:
    explicit Transition(float value = 0.0F, float sharpness = 16.0F);
    void snap(float value);
    void retarget(float target);
    void update(float delta);
    [[nodiscard]] float value() const;
    [[nodiscard]] float target() const;
    [[nodiscard]] bool settled(float epsilon = 0.002F) const;
    void setSharpness(float sharpness);

 private:
    float value_ = 0;
    float target_ = 0;
    float sharpness_ = 16;
};

// One-shot animation with an explicit clock: start(), update(), read progress.
// Restarting is explicit, so a rapidly fired action does not stutter.
class Clock {
 public:
    void start(float duration, float delay = 0.0F);
    void update(float delta);
    void finish();
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool waiting() const;  // inside the delay window
    [[nodiscard]] float elapsed() const;
    [[nodiscard]] float duration() const;
    [[nodiscard]] float remaining() const;
    [[nodiscard]] bool completed() const;
    [[nodiscard]] float linear() const;  // clamped 0..1
    [[nodiscard]] float eased() const;
    [[nodiscard]] float decay() const;  // 1 -> 0 eased, for impact flashes

 private:
    float elapsed_ = 0;
    float duration_ = 0;
    float delay_ = 0;
    bool running_ = false;
    bool completed_ = false;
};

// ------------------------------------------------------------- particles ---

// Particles carry their purpose so the renderer can drop decorative motes
// behind a modal without losing combat feedback.
enum class ParticleKind { SPARK, AMBIENT, CONFETTI };

struct Particle {
    ParticleKind kind = ParticleKind::SPARK;
    float x = 0;
    float y = 0;
    float velocity_x = 0;
    float velocity_y = 0;
    float life = 0;
    float max_life = 1;
    float size = 2;
    float rotation = 0;
    float spin = 0;
    float gravity = 0;
    float drag = 0;
    Color color{255, 255, 255, 255};
};

struct Ring {
    float x = 0;
    float y = 0;
    float radius = 0;
    float growth = 60;
    float life = 0;
    float max_life = 0.4F;
    float thickness = 2;
    Color color{255, 255, 255, 255};
};

struct Beam {
    float from_x = 0;
    float from_y = 0;
    float to_x = 0;
    float to_y = 0;
    float width = 5;
    float life = 0;
    float max_life = 0.3F;
    Color color{255, 255, 255, 255};
};

struct Floater {
    float x = 0;
    float y = 0;
    float life = 0;
    float max_life = 1;
    float rise = 30;
    float drift = 0;
    Color color{255, 255, 255, 255};
    std::string text;
};

struct EffectSettings {
    bool motion = true;     // false keeps opacity feedback and drops movement
    bool particles = true;
    bool shake = true;
    float intensity = 1.0F;
    // Intensity scaled by the motion gate; every optional effect routes
    // through this so the reduced-motion path is a single switch.
    [[nodiscard]] float scale(float value) const;
};

// Fixed-capacity pool: spawning past the cap drops the request instead of
// allocating, so a burst storm cannot cost a frame.
class Particles {
 public:
    explicit Particles(int capacity = 640);
    void clear();
    void spawn(Vector2 position, Vector2 velocity, float life, float size, Color color, float gravity = 0.0F,
               float drag = 0.0F);
    void spawn(ParticleKind kind, Vector2 position, Vector2 velocity, float life, float size, Color color);
    // Full-control variant for effects that need rotation or spin.
    void spawn(const Particle& particle);
    // Radial burst; angle/spread let impact sparks inherit a direction.
    void burst(Vector2 origin, int count, float speed, float life, float size, Color color, Rng& rng,
               float angle = 0.0F, float spread = 6.2831855F);
    void update(float delta);
    [[nodiscard]] const std::vector<Particle>& items() const;
    [[nodiscard]] int live() const;
    [[nodiscard]] std::size_t capacity() const;

 private:
    std::vector<Particle> items_;
    std::size_t capacity_;
};

// Trauma-driven shake: amplitude is quadratic in trauma and applied as whole
// pixels, so shaken text stays on the pixel grid instead of going soft.
class Shake {
 public:
    void add(float trauma);
    void update(float delta);
    void clear();
    void setLimit(float pixels);
    [[nodiscard]] Vector2 offset(float time) const;
    [[nodiscard]] float trauma() const;
    [[nodiscard]] bool active() const;

 private:
    float trauma_ = 0;
    float limit_ = 4.5F;
};

// ----------------------------------------------------------------- state ---

struct UnitSnapshot {
    std::uint64_t id = 0;
    int life = 0;
    int max_life = 1;
    int shield = 0;
    bool exhausted = false;
};

struct PlayerSnapshot {
    int life = 1;
    int max_life = 1;
    int mana = 0;
    int max_mana = 1;
    int shield = 0;
    int hand = 0;
    int deck = 0;
    int graveyard = 0;
};

struct StackSnapshot {
    bool present = false;
    int kind = 0;
    int controller = 0;
    std::uint64_t attacker = 0;
    int target_player = 0;
    std::uint64_t target_unit = 0;
};

struct Snapshot {
    std::uint64_t turn = 0;
    int phase = 0;
    int active = 0;
    int priority = 0;
    int passes = 0;
    bool finished = false;
    int winner = -1;
    int stack_size = 0;
    int scheduled = 0;
    PlayerSnapshot players[2];
    std::vector<UnitSnapshot> units[2];
    StackSnapshot top;
};

// Dying unit kept on screen for one short dissolve after it leaves the board.
struct Ghost {
    Rectangle bounds{};
    std::string label;
    Color accent{255, 255, 255, 255};
    float life = 0;
    float max_life = 1;
    float seed = 0;
};

// Diffs consecutive game snapshots and turns the differences into motion:
// summons, impacts, deaths, life loss, mana gain, stack pushes, turn changes
// and the finished celebration.
class Director {
 public:
    explicit Director(std::uint32_t seed = 20261005U);

    void reset();
    [[nodiscard]] EffectSettings& settings();
    [[nodiscard]] const EffectSettings& settings() const;
    [[nodiscard]] float time() const;

    // Frame hooks, in order: advance() ticks what already exists, the board is
    // drawn (which also reports placements), then observe() diffs the game
    // snapshot and draw() paints the overlay layer.
    void advance(float delta);
    void observe(const Snapshot& snapshot);

    // Placement cache, refreshed by the board drawing code each frame. Safe to
    // call on a const director: it is a render-side cache, not game state.
    void placePlayer(int index, Rectangle bounds, Color accent) const;
    void placeUnit(std::uint64_t id, Rectangle bounds, std::string label, Color accent) const;
    void placeStack(Rectangle bounds) const;
    void placeRail(Rectangle bounds) const;

    // Queries for the board: entrances rise and fade, impacts flash, bars ease
    // toward the new value while the numbers themselves stay still.
    [[nodiscard]] float unitEnter(std::uint64_t id) const;
    [[nodiscard]] float unitHit(std::uint64_t id) const;
    [[nodiscard]] float unitShield(std::uint64_t id) const;
    [[nodiscard]] float unitLife(std::uint64_t id, float target) const;
    [[nodiscard]] float playerHit(int index) const;
    [[nodiscard]] float playerLife(int index, float target) const;
    [[nodiscard]] float playerMana(int index, float target) const;
    [[nodiscard]] float stackArrival() const;
    [[nodiscard]] float turnArrival() const;
    [[nodiscard]] Vector2 shake() const;
    [[nodiscard]] float flashAlpha() const;
    [[nodiscard]] Color flashColor() const;
    [[nodiscard]] float celebration() const;
    [[nodiscard]] unsigned int effectCount() const;

    [[nodiscard]] const std::vector<Particle>& particleList() const;
    [[nodiscard]] const std::vector<Ring>& ringList() const;
    [[nodiscard]] const std::vector<Beam>& beamList() const;
    [[nodiscard]] const std::vector<Floater>& floaterList() const;
    [[nodiscard]] const std::vector<Ghost>& ghostList() const;

 private:
    struct UnitFx {
        std::uint64_t id = 0;
        Clock enter;
        Clock hit;
        Clock guard;
        Transition life{1.0F, 18.0F};
        Rectangle bounds{};
        std::string label;
        Color accent{255, 255, 255, 255};
        int last_life = 0;
        int last_shield = 0;
        bool alive = true;
        bool placed = false;
        bool established = false;
        bool pending_summon = false;
    };
    struct PlayerFx {
        Clock hit;
        Transition life{1.0F, 10.0F};
        Transition mana{0.0F, 12.0F};
        Rectangle bounds{};
        Color accent{255, 255, 255, 255};
        bool placed = false;
        float last_shield = 0;
    };
    [[nodiscard]] UnitFx* findUnit(std::uint64_t id) const;
    void applyEffects(float delta);
    void spawnImpact(const UnitFx& unit, int damage, int shield_gain, int life_after);
    void spawnPlayerDamage(int index, int damage, int life_after);
    void spawnStackEffect(const Snapshot& snapshot);
    void spawnAmbient(float delta);

    mutable std::vector<UnitFx> units_;
    mutable std::vector<Ghost> ghosts_;
    mutable PlayerFx players_[2];
    mutable Particles particles_;
    mutable std::vector<Ring> rings_;
    mutable std::vector<Beam> beams_;
    mutable std::vector<Floater> floaters_;
    mutable Shake shake_;
    mutable Rectangle stack_bounds_{};
    mutable Rectangle rail_bounds_{};
    mutable Color flash_color_{255, 255, 255, 255};
    mutable float flash_alpha_ = 0;
    mutable Clock stack_arrival_;
    mutable Clock turn_arrival_;
    mutable Clock celebration_;
    mutable float ambient_timer_ = 0;
    mutable float time_ = 0;
    mutable bool observed_ = false;
    mutable std::uint64_t turn_ = 0;
    mutable int stack_size_ = 0;
    mutable bool finished_ = false;
    mutable int previous_life_[2]{-1, -1};
    mutable int previous_shield_[2]{0, 0};
    mutable int previous_mana_[2]{-1, -1};
    mutable Rng rng_;
    EffectSettings settings_{};
};

}  // namespace cardis::fx
