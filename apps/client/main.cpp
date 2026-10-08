#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <raylib.h>
#include <raygui.h>
#include <rlgl.h>

#include "cardis/core/game.hpp"
#include "cardis/runtime/runtime.hpp"
#include "effects.hpp"
#include "layout.hpp"

namespace {
namespace fx = cardis::fx;
namespace ui = cardis::ui;

constexpr int WIDTH = static_cast<int>(ui::kCanvasWidth);
constexpr int HEIGHT = static_cast<int>(ui::kCanvasHeight);

// A duel lit from both ends: deep ink stage, the two characters' colours as
// ambient light, and everything else quiet so the board stays readable.
constexpr Color BACKGROUND{13, 12, 22, 255};
constexpr Color PANEL{27, 28, 43, 255};
constexpr Color PANEL_RAISED{36, 35, 51, 255};
constexpr Color PANEL_DEEP{21, 21, 34, 255};
constexpr Color BORDER{53, 51, 70, 255};
constexpr Color BORDER_SOFT{40, 39, 56, 255};
constexpr Color ACCENT_PINK{244, 170, 197, 255};
constexpr Color ACCENT_CYAN{148, 215, 220, 255};
constexpr Color CYAN{148, 215, 220, 255};
constexpr Color CREAM{246, 229, 192, 255};
constexpr Color INK{246, 240, 246, 255};
constexpr Color MUTED{184, 181, 201, 255};
constexpr Color MUTED_DIM{137, 135, 158, 255};
constexpr Color DANGER{255, 122, 138, 255};
constexpr Color SUCCESS{159, 230, 176, 255};
constexpr Color SHADOW{3, 3, 8, 255};

[[nodiscard]] Color PlayerAccent(cardis::PlayerId player) {
    return player == cardis::PlayerId::FIRST ? ACCENT_PINK : ACCENT_CYAN;
}

// Interface-level switches the immediate-mode helpers can read without a
// parameter on every call. motion off keeps colour feedback and drops
// movement, which is the reduced-motion contract for this client.
struct UiRuntime {
    bool motion = true;
};
UiRuntime& Runtime() {
    static UiRuntime runtime;
    return runtime;
}

// Staggered reveal used by the overlay panels: the row index delays the fade,
// bounded so a long list still settles well inside the motion budget.
[[nodiscard]] float Reveal(float elapsed, int index, float step = 0.04F, float duration = 0.16F) {
    return fx::Reveal(elapsed, std::min(index, 5), step, duration);
}

class Window {
 public:
    Window() {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
        InitWindow(WIDTH, HEIGHT, "Cardis | Character Duel");
        if (!IsWindowReady()) {
            throw std::runtime_error("Unable to initialize the graphics window");
        }
        SetWindowMinSize(960, 600);
        SetTargetFPS(60);
    }
    ~Window() { CloseWindow(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
};

class Canvas {
 public:
    void begin(Vector2 shake) {
        const float screen_width = static_cast<float>(GetScreenWidth());
        const float screen_height = static_cast<float>(GetScreenHeight());
        scale_ = std::min(screen_width / static_cast<float>(WIDTH), screen_height / static_cast<float>(HEIGHT));
        origin_ = {std::round((screen_width - static_cast<float>(WIDTH) * scale_) / 2),
                   std::round((screen_height - static_cast<float>(HEIGHT) * scale_) / 2)};
        // Shake is whole pixels so the font atlas stays on the pixel grid.
        const Vector2 offset{origin_.x + shake.x, origin_.y + shake.y};
        SetMouseOffset(-static_cast<int>(offset.x), -static_cast<int>(offset.y));
        SetMouseScale(1 / scale_, 1 / scale_);
        BeginDrawing();
        ClearBackground(BACKGROUND);
        BeginMode2D({offset, {0, 0}, 0, scale_});
    }
    void end(const std::string& screenshot) const {
        EndMode2D();
        if (!screenshot.empty()) {
            rlDrawRenderBatchActive();
            const auto capture = LoadImageFromScreen();
            const bool saved = ExportImage(capture, screenshot.c_str());
            UnloadImage(capture);
            if (!saved) {
                EndDrawing();
                throw std::runtime_error("Cannot save screenshot: " + screenshot);
            }
        }
        EndDrawing();
    }
    [[nodiscard]] float scale() const noexcept { return scale_; }
    [[nodiscard]] Vector2 origin() const noexcept { return origin_; }

 private:
    float scale_ = 1;
    Vector2 origin_{};
};

// Stage backdrop: one pre-rendered texture with the vertical wash, the
// vignette and a dither pass that keeps an 8-bit gradient from banding.
// Generated once, drawn pinned to the canvas so a shake cannot expose an edge.
class Backdrop {
 public:
    Backdrop() {
        const int width = WIDTH + 16;
        const int height = HEIGHT + 16;
        Image image = GenImageColor(width, height, BACKGROUND);
        auto* pixels = static_cast<Color*>(image.data);
        std::uint32_t noise = 0x2545F491U;
        for (int y = 0; y < height; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(height - 1);
            for (int x = 0; x < width; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(width - 1);
                noise = noise * 1664525U + 1013904223U;
                const float jitter = static_cast<float>((noise >> 24) & 0xFFU) / 255.0F - 0.5F;
                const float dx = (u - 0.5F) * 1.16F;
                const float dy = (v - 0.5F) * 1.06F;
                const float radial = fx::Sqrt(dx * dx + dy * dy);
                const float vignette = std::clamp(1.0F - radial * radial * 0.95F, 0.55F, 1.0F);
                const float warm = (1.0F - v) * (1.0F - u) * 0.55F;
                Color colour{};
                const float red = 15.0F + 16.0F * (1.0F - v) + 12.0F * warm;
                const float green = 14.0F + 6.0F * (1.0F - v) + 2.0F * (1.0F - u);
                const float blue = 24.0F + 14.0F * (1.0F - v) + 6.0F * u;
                colour.r = static_cast<unsigned char>(std::clamp(red * vignette + jitter * 1.6F, 0.0F, 255.0F));
                colour.g = static_cast<unsigned char>(std::clamp(green * vignette + jitter * 1.6F, 0.0F, 255.0F));
                colour.b = static_cast<unsigned char>(std::clamp(blue * vignette + jitter * 1.6F, 0.0F, 255.0F));
                colour.a = 255;
                pixels[y * width + x] = colour;
            }
        }
        texture_ = LoadTextureFromImage(image);
        UnloadImage(image);
        SetTextureFilter(texture_, TEXTURE_FILTER_BILINEAR);
    }
    ~Backdrop() {
        if (texture_.id != 0) {
            UnloadTexture(texture_);
        }
    }
    Backdrop(const Backdrop&) = delete;
    Backdrop& operator=(const Backdrop&) = delete;
    void draw(Vector2 shake) const {
        DrawTexture(texture_, static_cast<int>(-8.0F - shake.x), static_cast<int>(-8.0F - shake.y), WHITE);
    }

 private:
    Texture2D texture_{};
};

class Typography {
 public:
    explicit Typography(const std::filesystem::path& root) {
        std::ifstream input(root / "fonts/glyphs.txt", std::ios::binary);
        if (!input) {
            throw std::runtime_error("Missing assets/fonts/glyphs.txt");
        }
        const std::string glyphs{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        int count = 0;
        int* codepoints = LoadCodepoints(glyphs.c_str(), &count);
        codepoints_.assign(codepoints, codepoints + count);
        UnloadCodepoints(codepoints);
        std::ifstream font_file(root / "fonts/NotoSansCJKsc-Regular.otf", std::ios::binary);
        if (!font_file) {
            throw std::runtime_error("Missing NotoSansCJKsc-Regular.otf");
        }
        font_bytes_.assign(std::istreambuf_iterator<char>(font_file), std::istreambuf_iterator<char>());
    }
    ~Typography() {
        for (const auto& [size, font] : fonts_) {
            UnloadFont(font);
        }
    }
    Typography(const Typography&) = delete;
    Typography& operator=(const Typography&) = delete;

    void setViewport(float scale, Vector2 origin) {
        scale_ = scale;
        origin_ = origin;
        density_ = static_cast<float>(GetRenderWidth()) / static_cast<float>(GetScreenWidth());
    }
    void text(const std::string& value, float x, float y, float size = 18, Color color = INK, float alpha = 1.0F) const {
        const auto& font = fontFor(size);
        const float pixel_scale = scale_ * density_;
        const Vector2 aligned{(std::round((x * scale_ + origin_.x) * density_) / density_ - origin_.x) / scale_,
                              (std::round((y * scale_ + origin_.y) * density_) / density_ - origin_.y) / scale_};
        // Font atlas texels map 1:1 to framebuffer pixels, even at fractional window scales.
        DrawTextEx(font, value.c_str(), aligned, static_cast<float>(font.baseSize) / pixel_scale, 0,
                   Fade(color, alpha));
    }
    [[nodiscard]] float width(const std::string& value, float size = 18) const { return measure(value, size); }
    // Centred value with a dark offset copy, so it stays legible on top of the
    // life and mana bars it labels.
    void centered(const std::string& value, float centre_x, float y, float size = 18, Color color = INK,
                  float alpha = 1.0F) const {
        const float x = centre_x - measure(value, size) * 0.5F;
        text(value, x + 1, y + 1, size, BACKGROUND, alpha * 0.85F);
        text(value, x, y, size, color, alpha);
    }
    void right(const std::string& value, float right_x, float y, float size = 18, Color color = INK,
               float alpha = 1.0F) const {
        text(value, right_x - measure(value, size), y, size, color, alpha);
    }
    void fit(const std::string& value, float x, float y, float width, float size = 18, Color color = INK,
             float alpha = 1.0F) const {
        if (measure(value, size) <= width) {
            text(value, x, y, size, color, alpha);
            return;
        }
        std::string clipped = value;
        while (!clipped.empty() && measure(clipped + "…", size) > width) {
            popCharacter(clipped);
        }
        text(clipped + "…", x, y, size, color, alpha);
    }
    void wrapped(const std::string& value, float x, float y, float width, int max_lines, float size = 18,
                 Color color = INK, float alpha = 1.0F) const {
        std::string line;
        int row = 0;
        const float line_height = static_cast<float>(fontFor(size).baseSize) / (scale_ * density_) + 3;
        for (std::size_t offset = 0; offset < value.size();) {
            int bytes = 0;
            static_cast<void>(GetCodepointNext(value.c_str() + offset, &bytes));
            const auto next = value.substr(offset, static_cast<std::size_t>(bytes));
            if (!line.empty() && measure(line + next, size) > width) {
                if (row == max_lines - 1) {
                    fit(line + value.substr(offset), x, y + static_cast<float>(row) * line_height, width, size, color,
                        alpha);
                    return;
                }
                text(line, x, y + static_cast<float>(row) * line_height, size, color, alpha);
                ++row;
                line.clear();
            }
            line += next;
            offset += static_cast<std::size_t>(bytes);
        }
        if (!line.empty()) {
            text(line, x, y + static_cast<float>(row) * line_height, size, color, alpha);
        }
    }

 private:
    [[nodiscard]] const Font& fontFor(float size) const {
        const int pixels = std::clamp(static_cast<int>(std::round(std::max(18.0F, size) * 1.35F * scale_ * density_)),
                                      static_cast<int>(std::ceil(22 * density_)), 192);
        if (const auto found = fonts_.find(pixels); found != fonts_.end()) {
            return found->second;
        }
        auto font = LoadFontFromMemory(".otf", font_bytes_.data(), static_cast<int>(font_bytes_.size()), pixels,
                                       codepoints_.data(), static_cast<int>(codepoints_.size()));
        if (font.texture.id == 0 || font.texture.id == GetFontDefault().texture.id) {
            throw std::runtime_error("Cannot rasterize Chinese font");
        }
        SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
        return fonts_.emplace(pixels, font).first->second;
    }
    [[nodiscard]] float measure(const std::string& value, float size) const {
        const auto& font = fontFor(size);
        return MeasureTextEx(font, value.c_str(), static_cast<float>(font.baseSize) / (scale_ * density_), 0).x;
    }
    static void popCharacter(std::string& value) {
        auto offset = value.size() - 1;
        while (offset > 0 && (static_cast<unsigned char>(value[offset]) & 0xC0) == 0x80) {
            --offset;
        }
        value.resize(offset);
    }
    std::vector<unsigned char> font_bytes_;
    mutable std::vector<int> codepoints_;
    mutable std::map<int, Font> fonts_;
    float scale_ = 1;
    float density_ = 1;
    Vector2 origin_{};
};

class Portraits {
 public:
    explicit Portraits(const std::filesystem::path& root) : root_(root) {}
    ~Portraits() {
        for (const auto& [path, texture] : textures_) {
            if (texture.id != 0) {
                UnloadTexture(texture);
            }
        }
    }
    Portraits(const Portraits&) = delete;
    Portraits& operator=(const Portraits&) = delete;
    Texture2D get(const std::string& path) {
        if (const auto found = textures_.find(path); found != textures_.end()) {
            return found->second;
        }
        Texture2D texture{};
        const auto full_path = (root_ / path).string();
        if (!path.empty() && FileExists(full_path.c_str())) {
            texture = LoadTexture(full_path.c_str());
            SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        }
        textures_.emplace(path, texture);
        return texture;
    }

 private:
    std::filesystem::path root_;
    std::map<std::string, Texture2D> textures_;
};

void Panel(Rectangle bounds, Color color = PANEL, float alpha = 1.0F) {
    const Rectangle shadow{bounds.x, bounds.y + 2, bounds.width, bounds.height};
    DrawRectangleRounded(shadow, 0.08F, 12, Fade(SHADOW, 0.34F * alpha));
    DrawRectangleRounded(bounds, 0.08F, 12, Fade(color, alpha));
    // A 1px lift along the top edge reads as depth without a gradient.
    DrawRectangleRoundedLinesEx({bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2}, 0.08F, 12, 1,
                                Fade(INK, 0.05F * alpha));
    DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 1, Fade(BORDER, alpha));
}

// Small rounded tag used for keywords, costs and states.
void Chip(Rectangle bounds, const std::string& label, const Typography& type, Color accent, bool filled = false,
          float alpha = 1.0F, float size = 18) {
    DrawRectangleRounded(bounds, 0.45F, 8, Fade(filled ? accent : PANEL_DEEP, (filled ? 0.9F : 0.85F) * alpha));
    DrawRectangleRoundedLinesEx(bounds, 0.45F, 8, 1, Fade(accent, (filled ? 0.95F : 0.55F) * alpha));
    type.centered(label, bounds.x + bounds.width * 0.5F, bounds.y + (bounds.height - size - 2.0F) * 0.5F + 1.0F, size,
                  filled ? BACKGROUND : accent, alpha);
}

// Buttons animate their own hover and press state, retargeting mid-flight so a
// quick pointer sweep blends instead of restarting.
struct ButtonMotion {
    fx::Transition hover{0.0F, 26.0F};
    fx::Clock press;
};
std::map<std::string, ButtonMotion>& ButtonMotions() {
    static std::map<std::string, ButtonMotion> motions;
    return motions;
}
bool Button(const Typography& type, Rectangle bounds, const std::string& label, bool primary = false,
            bool enabled = true, float alpha = 1.0F) {
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    if (hovered) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }
    auto& motion = ButtonMotions()[label];
    if (Runtime().motion) {
        motion.hover.retarget(hovered ? 1.0F : 0.0F);
        motion.hover.update(GetFrameTime());
    } else {
        motion.hover.snap(hovered ? 1.0F : 0.0F);
    }
    motion.press.update(GetFrameTime());
    const float lift = motion.hover.value();
    const float press = motion.press.running() ? motion.press.decay() : 0.0F;
    Color fill = !enabled    ? PANEL_DEEP
                 : primary   ? ColorLerp(ACCENT_PINK, CREAM, lift)
                             : ColorLerp(PANEL, PANEL_RAISED, lift);
    if (press > 0.0F) {
        fill = ColorLerp(fill, primary ? INK : BORDER, press * 0.5F);
    }
    DrawRectangleRounded(bounds, 0.22F, 12, Fade(fill, alpha));
    if (!primary) {
        DrawRectangleRoundedLinesEx(bounds, 0.22F, 12, 1, Fade(ColorLerp(BORDER_SOFT, ACCENT_PINK, lift), alpha));
    }
    if (hovered && Runtime().motion) {
        DrawRectangleRoundedLinesEx(bounds, 0.22F, 12, 1, Fade(primary ? CREAM : ACCENT_PINK, 0.35F * alpha));
    }
    type.fit(label, bounds.x + 18, bounds.y + (bounds.height - 19.0F) * 0.5F, bounds.width - 36, 19,
             !enabled  ? MUTED_DIM
             : primary ? BACKGROUND
                       : INK,
             alpha);
    // raygui owns control hit testing; drawing above supplies the game's visual style.
    GuiSetAlpha(0);
    if (!enabled) {
        GuiDisable();
    }
    const bool pressed = GuiButton(bounds, "") != 0;
    GuiEnable();
    GuiSetAlpha(1);
    if (pressed && enabled) {
        motion.press.start(fx::dur::kPress);
    }
    return pressed && enabled;
}

// Both halves of the board are lit by their own character; the acting side
// burns brighter, which is the spatial cue for whose turn it is.
void DrawStageGlow(const fx::Director& effects, cardis::PlayerId active) {
    const float time = effects.time();
    const bool motion = Runtime().motion;
    const float drift = motion ? 1.0F : 0.0F;
    const Vector2 pink{430.0F + 26.0F * fx::Sin(time * 0.31F) * drift, 690.0F + 18.0F * fx::Cos(time * 0.24F) * drift};
    const Vector2 cyan{1010.0F + 30.0F * fx::Cos(time * 0.27F) * drift,
                       186.0F + 22.0F * fx::Sin(time * 0.35F) * drift};
    const bool first_active = active == cardis::PlayerId::FIRST;
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(static_cast<int>(pink.x), static_cast<int>(pink.y), 620,
                       Fade(ACCENT_PINK, first_active ? 0.16F : 0.085F), Fade(ACCENT_PINK, 0.0F));
    DrawCircleGradient(static_cast<int>(cyan.x), static_cast<int>(cyan.y), 620,
                       Fade(ACCENT_CYAN, first_active ? 0.085F : 0.16F), Fade(ACCENT_CYAN, 0.0F));
    EndBlendMode();
    if (first_active) {
        DrawRectangleGradientV(284, 374, 848, 298, Fade(ACCENT_PINK, 0.0F), Fade(ACCENT_PINK, 0.07F));
    } else {
        DrawRectangleGradientV(284, 78, 848, 296, Fade(ACCENT_CYAN, 0.07F), Fade(ACCENT_CYAN, 0.0F));
    }
}

// Everything the effects director spawned this frame, painted above the board.
void DrawEffectOverlay(const Typography& type, const fx::Director& effects, bool overlay_open) {
    for (const auto& ghost : effects.ghostList()) {
        const float progress = fx::Saturate(ghost.life / ghost.max_life);
        const float eased = fx::EaseOutCubic(progress);
        const Rectangle bounds{ghost.bounds.x, ghost.bounds.y + (1.0F - eased) * 12.0F, ghost.bounds.width,
                               ghost.bounds.height};
        DrawRectangleRounded(bounds, 0.08F, 12, Fade(ghost.accent, 0.16F * progress));
        DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 2, Fade(ghost.accent, 0.55F * progress));
        type.fit(ghost.label, bounds.x + 14, bounds.y + 8, 200, 18, ghost.accent, progress * 0.8F);
    }
    BeginBlendMode(BLEND_ADDITIVE);
    for (const auto& beam : effects.beamList()) {
        const float progress = fx::Saturate(beam.life / beam.max_life);
        const Vector2 from{beam.from_x, beam.from_y};
        const Vector2 to{beam.to_x, beam.to_y};
        const Vector2 delta{to.x - from.x, to.y - from.y};
        const Vector2 normal{-delta.y, delta.x};
        const float length = std::max(1.0F, fx::Sqrt(delta.x * delta.x + delta.y * delta.y));
        const Vector2 unit{normal.x / length, normal.y / length};
        const float taper = beam.width * progress;
        for (int layer = 0; layer < 3; ++layer) {
            const float spread = static_cast<float>(layer) * 1.6F;
            const float width = std::max(0.5F, taper - spread);
            DrawLineEx({from.x - unit.x * width, from.y - unit.y * width}, {to.x - unit.x * width * 0.4F,
                                                                            to.y - unit.y * width * 0.4F},
                       width * 0.9F, Fade(beam.color, 0.30F * progress));
            DrawLineEx({from.x + unit.x * width, from.y + unit.y * width}, {to.x + unit.x * width * 0.4F,
                                                                            to.y + unit.y * width * 0.4F},
                       width * 0.9F, Fade(beam.color, 0.30F * progress));
        }
        DrawLineEx(from, to, std::max(1.0F, beam.width * progress * 0.8F), Fade(CREAM, 0.55F * progress));
    }
    for (const auto& ring : effects.ringList()) {
        const float progress = fx::Saturate(ring.life / ring.max_life);
        DrawRing({ring.x, ring.y}, std::max(0.5F, ring.radius - ring.thickness), ring.radius, 0.0F, 360.0F, 40,
                 Fade(ring.color, 0.55F * progress * progress));
    }
    // Sparks glow; ambient stage dust is decorative, so it steps aside while a
    // panel owns the screen; confetti keeps its own colour.
    for (const auto& particle : effects.particleList()) {
        if (particle.kind == fx::ParticleKind::CONFETTI) {
            continue;
        }
        if (particle.kind == fx::ParticleKind::AMBIENT && overlay_open) {
            continue;
        }
        const float progress = fx::Saturate(particle.life / particle.max_life);
        const float size = std::max(0.6F, particle.size * (0.35F + 0.65F * progress));
        DrawCircleV({particle.x, particle.y}, size, Fade(particle.color, 0.8F * progress));
    }
    EndBlendMode();
    for (const auto& particle : effects.particleList()) {
        if (particle.kind != fx::ParticleKind::CONFETTI) {
            continue;
        }
        const float progress = fx::Saturate(particle.life / particle.max_life);
        const Rectangle bounds{particle.x, particle.y, particle.size, particle.size * 0.6F};
        DrawRectanglePro(bounds, {bounds.width * 0.5F, bounds.height * 0.5F}, particle.rotation,
                         Fade(particle.color, progress));
    }
    for (const auto& floater : effects.floaterList()) {
        const float progress = 1.0F - fx::Saturate(floater.life / floater.max_life);
        const float alpha = fx::Saturate(progress / 0.12F) * fx::Saturate((1.0F - progress) / 0.5F);
        type.centered(floater.text, floater.x, floater.y, 24, floater.color, alpha);
    }
    if (effects.flashAlpha() > 0.001F) {
        DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(effects.flashColor(), effects.flashAlpha()));
    }
}

std::string StepText(const cardis::EffectStep& step) {
    const std::string recipient = step.recipient == cardis::EffectRecipient::CONTROLLER ? "己方"
                                  : step.recipient == cardis::EffectRecipient::OPPONENT ? "对方"
                                                                                        : "目标";
    return recipient +
           (step.effect == cardis::EffectKind::DAMAGE ? "伤害"
            : step.effect == cardis::EffectKind::HEAL ? "治疗"
                                                      : "护盾") +
           std::to_string(step.amount);
}
std::string TimedEffects(const cardis::CardDefinition& card, cardis::EffectTiming timing) {
    std::string value;
    for (const auto& step : cardis::EffectsOf(card)) {
        if (step.timing == timing) {
            value += (value.empty() ? "" : " / ") + StepText(step);
        }
    }
    return value.empty() ? "无" : value;
}
std::string EffectDescription(const cardis::CardDefinition& card) {
    return "当前：" + TimedEffects(card, cardis::EffectTiming::ON_RESOLVE) + "；回合末：" +
           TimedEffects(card, cardis::EffectTiming::END_OF_TURN);
}

std::string GenderLabel(cardis::Gender gender) {
    switch (gender) {
        case cardis::Gender::FEMALE:
            return "女";
        case cardis::Gender::MALE:
            return "男";
        case cardis::Gender::NON_BINARY:
            return "非二元";
        case cardis::Gender::UNSPECIFIED:
            return "未设定";
    }
    return "未设定";
}

void DrawCharacterDetails(const Typography& type, Portraits& portraits, const cardis::CharacterRoster& roster,
                          const cardis::Game& game, cardis::PlayerId inspected, bool& open, float reveal) {
    if (reveal <= 0.0F) {
        return;
    }
    const auto& character = roster.character(roster.player_character_ids[cardis::Index(inspected)]);
    DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BLACK, 0.80F * Reveal(reveal, 0, 0.04F)));
    const float panel_alpha = Reveal(reveal, 0, 0.04F);
    Panel({170, 94, 1100, 712}, PANEL, panel_alpha);
    DrawRectangleGradientH(170, 94, 1100, 4, Fade(ACCENT_PINK, 0.85F * panel_alpha),
                           Fade(CYAN, 0.85F * panel_alpha));
    type.text(character.name, 210, 120, 34, INK, Reveal(reveal, 1));
    type.fit(character.original_name + " / " + character.title, 210, 178, 320, 18, ACCENT_PINK, Reveal(reveal, 2));
    DrawRectangleRounded({210, 210, 96.0F * Reveal(reveal, 2), 3}, 0.5F, 6, Fade(ACCENT_PINK, 0.8F));
    const auto portrait = portraits.get(character.portrait);
    const float art_alpha = Reveal(reveal, 3);
    if (portrait.id != 0 && art_alpha > 0.0F) {
        const float scale = std::min(300.0F / static_cast<float>(portrait.width),
                                     535.0F / static_cast<float>(portrait.height));
        const Rectangle destination{215, 242, static_cast<float>(portrait.width) * scale,
                                    static_cast<float>(portrait.height) * scale};
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient(static_cast<int>(destination.x + destination.width * 0.5F),
                           static_cast<int>(destination.y + destination.height * 0.55F), 250,
                           Fade(ACCENT_PINK, 0.16F * art_alpha), Fade(ACCENT_PINK, 0.0F));
        EndBlendMode();
        DrawTexturePro(portrait, {0, 0, static_cast<float>(portrait.width), static_cast<float>(portrait.height)},
                       destination, {0, 0}, 0, Fade(WHITE, art_alpha));
        DrawRectangleLinesEx({destination.x - 1, destination.y - 1, destination.width + 2, destination.height + 2}, 1,
                             Fade(BORDER, art_alpha));
    }
    if (Button(type, {1110, 116, 132, 40}, "关闭 ×", false, true, Reveal(reveal, 1))) {
        open = false;
    }
    type.text("作品资料", 560, 127, 23, ACCENT_PINK, Reveal(reveal, 3));
    type.fit("作品  " + character.origin, 560, 180, 650, 18, INK, Reveal(reveal, 4));
    type.fit("组别  " + roster.group(character.group_id).name + "  ·  " + GenderLabel(character.gender) + "  ·  " +
                 character.species,
             560, 216, 650, 18, INK, Reveal(reveal, 4));
    type.fit("身份  " + character.occupation, 560, 252, 650, 18, INK, Reveal(reveal, 5));
    type.wrapped(character.biography, 560, 292, 646, 2, 18, MUTED, Reveal(reveal, 5));
    type.text("游戏设定", 560, 373, 23, CYAN, Reveal(reveal, 6));
    type.fit(character.combat_role + "  /  初始生命 " + std::to_string(character.starting_life) + "  /  法力上限 " +
                 std::to_string(character.max_mana),
             560, 418, 650, 18, INK, Reveal(reveal, 7));
    std::string tags = "标签  ";
    for (const auto& tag : character.tags) {
        tags += tag + "   ";
    }
    type.fit(tags, 560, 454, 650, 18, MUTED, Reveal(reveal, 7));
    float y = 502;
    int row = 0;
    for (const auto& skill_id : character.skill_ids) {
        if (y > 702) {
            break;
        }
        const auto skill = std::find_if(game.cards().begin(), game.cards().end(),
                                        [&skill_id](const auto& card) { return card.id == skill_id; });
        if (skill == game.cards().end()) {
            continue;
        }
        const float alpha = Reveal(reveal, 8 + row);
        Panel({556, y, 676, 65}, PANEL_RAISED, alpha);
        DrawRectangleRounded({556, y + 8, 3, 49}, 0.5F, 6, Fade(ACCENT_CYAN, 0.7F * alpha));
        type.fit(skill->name, 573, y + 8, 200, 20, INK, alpha);
        Chip({790, y + 7, 52, 22}, "费 " + std::to_string(skill->cost), type, CREAM, false, alpha, 18);
        type.fit(EffectDescription(*skill), 856, y + 12, 362, 18, MUTED, alpha);
        type.fit(skill->kind == cardis::CardKind::CHARACTER
                     ? "角色 " + std::to_string(skill->attack) + " 攻 / " + std::to_string(skill->health) + " 血"
                     : "技能 · 主要阶段施放",
                 573, y + 38, 640, 18, MUTED_DIM, alpha);
        y += 76;
        ++row;
    }
    type.text("定位、称号与技能为 Cardis 演示设计。", 560, 759, 18, MUTED_DIM, Reveal(reveal, 12));
}

struct Selection {
    int hand = -1;
    std::uint64_t unit = 0;
    void clear() {
        hand = -1;
        unit = 0;
    }
};
struct TargetClick {
    bool clicked = false;
    cardis::Target target{};
    cardis::Row row = cardis::Row::FRONT;
};
bool Clicked(Rectangle bounds, bool interactive) {
    return interactive && CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
cardis::ActionResult CanTarget(const cardis::Game& game, const Selection& selection, cardis::Target target,
                               cardis::Row row = cardis::Row::FRONT) {
    if (selection.hand >= 0) {
        return game.canCast(game.state().priority, static_cast<std::size_t>(selection.hand), target, row);
    }
    if (selection.unit != 0) {
        return game.canAttack(game.state().priority, selection.unit, target);
    }
    return {};
}
bool CanPlayCard(const cardis::Game& game, std::size_t index) {
    const auto owner = game.state().priority;
    const auto& card = game.cards()[game.state().players[cardis::Index(owner)].hand[index]];
    if (card.kind == cardis::CardKind::CHARACTER) {
        return game.canCast(owner, index, cardis::Target{owner, 0}, cardis::Row::FRONT).accepted ||
               game.canCast(owner, index, cardis::Target{owner, 0}, cardis::Row::BACK).accepted;
    }
    for (const auto player : {cardis::PlayerId::FIRST, cardis::PlayerId::SECOND}) {
        if (game.canCast(owner, index, cardis::Target{player, 0}).accepted) {
            return true;
        }
        for (const auto& unit : game.state().players[cardis::Index(player)].battlefield) {
            if (game.canCast(owner, index, cardis::Target{player, unit.id}).accepted) {
                return true;
            }
        }
    }
    return false;
}
bool CanAttackWith(const cardis::Game& game, std::uint64_t unit) {
    const auto player = game.state().priority;
    const auto enemy = cardis::Opponent(player);
    if (game.canAttack(player, unit, {enemy, 0}).accepted) {
        return true;
    }
    for (const auto& target : game.state().players[cardis::Index(enemy)].battlefield) {
        if (game.canAttack(player, unit, {enemy, target.id}).accepted) {
            return true;
        }
    }
    return false;
}
bool HasAction(const cardis::Game& game) {
    const auto& player = game.state().players[cardis::Index(game.state().priority)];
    for (std::size_t i = 0; i < player.hand.size(); ++i) {
        if (CanPlayCard(game, i)) {
            return true;
        }
    }
    for (const auto& unit : player.battlefield) {
        if (CanAttackWith(game, unit.id) || game.canMove(game.state().priority, unit.id).accepted) {
            return true;
        }
    }
    return false;
}
std::string PhaseName(cardis::Phase phase) {
    return phase == cardis::Phase::MAIN     ? "主要阶段"
           : phase == cardis::Phase::COMBAT ? "战斗阶段"
           : phase == cardis::Phase::END    ? "结束阶段"
                                            : "对局结束";
}
std::string PassLabel(const cardis::GameState& state) {
    if (!state.stack.empty()) {
        return state.consecutive_passes == 0 ? "让对手响应" : "确认结算";
    }
    if (state.consecutive_passes == 0) {
        return "让过优先权";
    }
    return state.phase == cardis::Phase::MAIN     ? "进入战斗"
           : state.phase == cardis::Phase::COMBAT ? "进入结束阶段"
                                                  : "结束回合";
}
std::string MessageLabel(std::string label);
// Full card text for the hovered or selected card. It lives in the left
// console instead of floating over the hand, so it never covers the board,
// the player bars or the hand header.
void DrawConsoleDetail(const Typography& type, const cardis::CardDefinition& card, float alpha) {
    const auto effects = cardis::EffectsOf(card);
    const bool is_character = card.kind == cardis::CardKind::CHARACTER;
    type.fit(card.name, 44, 724, 208, 20, INK, alpha);
    type.fit("费用 " + std::to_string(card.cost) + " · " + (is_character ? "角色牌" : "技能牌"), 44, 752, 208, 18,
             MUTED, alpha);
    if (is_character) {
        type.fit(std::to_string(card.attack) + " 攻 / " + std::to_string(card.health) + " 血" +
                     (card.guard ? " · 守护" : "") + (card.haste ? " · 疾奏" : ""),
                 44, 778, 208, 18, CYAN, alpha);
        return;
    }
    float y = 778.0F;
    for (const auto& effect : effects) {
        if (y > 834.0F) {
            return;
        }
        type.fit(std::string(effect.timing == cardis::EffectTiming::ON_RESOLVE ? "当前 " : "回合末 ") +
                     StepText(effect),
                 44, y, 208, 18, effect.timing == cardis::EffectTiming::ON_RESOLVE ? INK : CREAM, alpha);
        y += 28.0F;
    }
    if (effects.empty()) {
        type.fit("没有效果条目。", 44, y, 208, 18, MUTED, alpha);
    }
}

void DrawCharacterPane(const Typography& type, Portraits& portraits, const cardis::CharacterRoster& roster,
                       cardis::PlayerId& inspected, bool& details, bool& timeline, const fx::Director& effects,
                       const cardis::CardDefinition* preview) {
    const auto& character = roster.character(roster.player_character_ids[cardis::Index(inspected)]);
    const Color accent = PlayerAccent(inspected);
    Panel(ui::CharacterPane(), Color{37, 29, 46, 255});
    DrawRectangleGradientH(28, 86, 238, 3, Fade(accent, 0.85F), Fade(accent, 0.0F));
    if (Button(type, {40, 98, 98, 38}, "P1", inspected == cardis::PlayerId::FIRST)) {
        inspected = cardis::PlayerId::FIRST;
    }
    if (Button(type, {154, 98, 98, 38}, "P2", inspected == cardis::PlayerId::SECOND)) {
        inspected = cardis::PlayerId::SECOND;
    }
    type.fit(character.name, 44, 151, 210, 27, INK);
    type.fit(character.title, 44, 196, 210, 18, ACCENT_PINK);
    const auto portrait = portraits.get(character.portrait);
    const float pulse = Runtime().motion ? fx::Pulse(effects.time(), 4.2F) : 0.0F;
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(150, 360, 118, Fade(accent, 0.10F + 0.05F * pulse), Fade(accent, 0.0F));
    EndBlendMode();
    if (portrait.id != 0) {
        const float scale = std::min(210.0F / static_cast<float>(portrait.width),
                                     275.0F / static_cast<float>(portrait.height));
        const Rectangle destination{147.0F - static_cast<float>(portrait.width) * scale * 0.5F, 239,
                                    static_cast<float>(portrait.width) * scale,
                                    static_cast<float>(portrait.height) * scale};
        DrawTexturePro(portrait, {0, 0, static_cast<float>(portrait.width), static_cast<float>(portrait.height)},
                       destination, {0, 0}, 0, WHITE);
    }
    Chip({44, 524, 96, 26}, roster.group(character.group_id).name, type, ACCENT_PINK, false, 1.0F, 18);
    Chip({146, 524, 108, 26}, GenderLabel(character.gender) + " · " + character.species, type, MUTED, false, 1.0F,
         18);
    type.fit(character.combat_role, 44, 562, 208, 18, CYAN);
    type.fit("初始生命 " + std::to_string(character.starting_life) + " · 灵力上限 " +
                 std::to_string(character.max_mana),
             44, 590, 208, 18, MUTED);
    if (Button(type, {40, 618, 214, 34}, "角色资料")) {
        details = true;
    }
    Panel(ui::ConsolePane());
    type.text("操作提示", 44, 690, 20, ACCENT_PINK);
    if (Button(type, {130, 686, 124, 27}, Runtime().motion ? "特效 开" : "特效 关", false)) {
        Runtime().motion = !Runtime().motion;
    }
    if (preview != nullptr) {
        DrawConsoleDetail(type, *preview, 1.0F);
        return;
    }
    type.fit("选牌后点目标", 44, 726, 208, 18, MUTED);
    type.fit("选前排后攻击", 44, 754, 208, 18, MUTED);
    type.fit("右键取消选择", 44, 782, 208, 18, MUTED);
    if (Button(type, {40, 812, 214, 32}, "结算与记录")) {
        timeline = true;
    }
}
void DrawPlayer(const Typography& type, Portraits& portraits, const cardis::Game& game,
                const cardis::CharacterRoster& roster, cardis::PlayerId id, bool top, const Selection& selection,
                TargetClick& click, bool interactive, const fx::Director& effects) {
    const auto& state = game.state();
    const auto& player = state.players[cardis::Index(id)];
    const auto& character = roster.character(player.character_id);
    const Rectangle bounds = ui::PlayerBar(top);
    const float y = bounds.y;
    const int index = static_cast<int>(cardis::Index(id));
    const Color accent = PlayerAccent(id);
    const bool priority = id == state.priority;
    const float hit = effects.playerHit(index);
    effects.placePlayer(index, bounds, accent);
    const bool placement =
        selection.hand >= 0 &&
        game.cards()[state.players[cardis::Index(state.priority)].hand[static_cast<std::size_t>(selection.hand)]]
                .kind == cardis::CardKind::CHARACTER;
    const bool legal = !placement && CanTarget(game, selection, {id, 0}).accepted;
    Panel(bounds, priority ? Color{41, 35, 53, 255} : PANEL);
    const float breathe = Runtime().motion ? fx::Pulse(effects.time(), 2.6F) : 0.0F;
    DrawRectangleRounded(ui::PriorityStripe(y), 1.0F, 4, Fade(accent, priority ? 0.95F : 0.30F));
    if (priority) {
        DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 2, Fade(accent, 0.45F + 0.30F * breathe));
    }
    if (legal) {
        DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 2, CYAN);
    }
    if (hit > 0.0F) {
        DrawRectangleRounded(bounds, 0.08F, 12, Fade(DANGER, hit * 0.26F));
    }
    const auto portrait = portraits.get(character.portrait);
    const Rectangle avatar = ui::Avatar(y);
    DrawRectangleRounded({avatar.x - 2, avatar.y - 2, avatar.width + 4, avatar.height + 4}, 0.3F, 8,
                         Fade(PANEL_DEEP, 0.95F));
    if (portrait.id != 0) {
        const float side = static_cast<float>(std::min(portrait.width, portrait.height)) * 0.62F;
        const Rectangle source{(static_cast<float>(portrait.width) - side) * 0.5F,
                               static_cast<float>(portrait.height) * 0.03F, side, side};
        DrawTexturePro(portrait, source, avatar, {0, 0}, 0, WHITE);
    } else {
        type.centered("P" + std::to_string(index + 1), avatar.x + avatar.width * 0.5F, avatar.y + 8, 18, MUTED);
    }
    DrawRectangleRoundedLinesEx({avatar.x - 2, avatar.y - 2, avatar.width + 4, avatar.height + 4}, 0.3F, 8, 1,
                                Fade(accent, priority ? 0.95F : 0.45F));
    type.fit("P" + std::to_string(index + 1) + "  " + character.name, ui::PlayerName(y).x, y + 8,
             ui::PlayerName(y).width, 22, INK);
    // Life: the bar eases to the new value, the number snaps. The dim red
    // remainder shows what was just lost.
    const Rectangle life = ui::LifeBar(y);
    const float life_target =
        player.max_life > 0 ? static_cast<float>(player.life) / static_cast<float>(player.max_life) : 0.0F;
    const float life_shown = fx::Saturate(effects.playerLife(index, life_target));
    DrawRectangleRounded(life, 0.45F, 8, Fade(Color{7, 7, 13, 255}, 0.92F));
    const auto life_segment = [&life](float fraction, Color color) {
        const float segment = std::max(0.0F, life.width * fx::Saturate(fraction));
        if (segment < 2.0F) {
            return;
        }
        DrawRectangleRounded({life.x, life.y, segment, life.height}, 0.45F, 8, color);
    };
    life_segment(std::max(life_shown, life_target), Fade(DANGER, 0.55F));
    life_segment(life_target, Fade(ColorLerp(accent, DANGER, 1.0F - fx::Saturate(life_target * 1.4F)), 0.95F));
    for (int tick = 5; tick < player.max_life; tick += 5) {
        const float position = life.x + life.width * static_cast<float>(tick) / static_cast<float>(player.max_life);
        DrawRectangle(static_cast<int>(position), static_cast<int>(life.y + 3), 1, static_cast<int>(life.height) - 6,
                      Fade(BACKGROUND, 0.55F));
    }
    DrawRectangleRoundedLinesEx(life, 0.45F, 8, 1, Fade(BORDER, 0.9F));
    type.centered(std::to_string(player.life) + " / " + std::to_string(player.max_life), life.x + life.width * 0.5F,
                  life.y + 0.5F, 18, INK);
    const Rectangle shield = ui::ShieldChip(y);
    const bool guarded = player.shield > 0;
    const float guard_pulse = guarded && Runtime().motion ? fx::Pulse(effects.time(), 3.4F) : 0.0F;
    DrawRectangleRounded(shield, 0.45F, 8, Fade(guarded ? Color{84, 71, 40, 255} : PANEL_DEEP, 0.9F));
    DrawRectangleRoundedLinesEx(shield, 0.45F, 8, 1,
                                Fade(guarded ? CREAM : BORDER_SOFT, guarded ? 0.65F + 0.3F * guard_pulse : 0.6F));
    type.centered("护 " + std::to_string(player.shield), shield.x + shield.width * 0.5F, shield.y + 0.5F, 18,
                  guarded ? CREAM : MUTED_DIM);
    // Mana pips: available capacity lit, locked slots dim, fill eased.
    const float mana_target =
        player.max_mana > 0 ? static_cast<float>(player.mana) / static_cast<float>(player.max_mana) : 0.0F;
    const float mana_shown = fx::Saturate(effects.playerMana(index, mana_target)) * static_cast<float>(ui::kManaPips);
    for (int pip = 0; pip < ui::kManaPips; ++pip) {
        const Rectangle slot = ui::ManaPip(y, pip);
        const bool available = pip < player.max_mana;
        DrawRectangleRounded(slot, 0.35F, 6, Fade(Color{15, 19, 29, 255}, 0.92F));
        DrawRectangleRoundedLinesEx(slot, 0.35F, 6, 1, Fade(available ? CYAN : BORDER_SOFT, available ? 0.32F : 0.22F));
        const float fill = fx::Saturate(mana_shown - static_cast<float>(pip));
        if (fill > 0.002F) {
            const float segment = std::max(2.0F, slot.width * fill);
            DrawRectangleRounded({slot.x, slot.y, segment, slot.height}, 0.35F, 6, Fade(CYAN, 1.0F));
        }
    }
    const bool full = player.max_mana > 0 && player.mana >= player.max_mana;
    type.centered("灵力 " + std::to_string(player.mana) + "/" + std::to_string(player.max_mana),
                  ui::ManaPipCentreX(), y + 52, 18, full && Runtime().motion ? CREAM : CYAN);
    type.fit("卡组 " + std::to_string(player.deck.size()) + "  ·  手牌 " + std::to_string(player.hand.size()) +
                 "  ·  墓地 " + std::to_string(player.graveyard.size()),
             ui::PlayerStats(y).x, y + 52, 420, 18, MUTED);
    if ((selection.hand >= 0 || selection.unit != 0) && !legal) {
        DrawRectangleRounded(bounds, 0.08F, 12, Fade(BACKGROUND, 0.20F));
    }
    if (Clicked(bounds, interactive) && !placement && (selection.hand >= 0 || selection.unit != 0)) {
        click = {true, {id, 0}, cardis::Row::FRONT};
    }
}
void DrawRow(const Typography& type, const cardis::Game& game, cardis::PlayerId owner, cardis::Row row, int band,
             Selection& selection, TargetClick& click, bool interactive, std::string& message,
             const fx::Director& effects) {
    std::vector<const cardis::UnitState*> units;
    for (const auto& unit : game.state().players[cardis::Index(owner)].battlefield) {
        if (unit.row == row) {
            units.push_back(&unit);
        }
    }
    const Color accent = PlayerAccent(owner);
    for (std::size_t column = 0; column < 3; ++column) {
        const Rectangle bounds = ui::Slot(band, static_cast<int>(column));
        const bool empty = column >= units.size();
        const auto target = cardis::Target{owner, empty ? 0 : units[column]->id};
        const bool placement =
            selection.hand >= 0 &&
            game.cards()[game.state().players[cardis::Index(game.state().priority)].hand[static_cast<std::size_t>(
                             selection.hand)]]
                    .kind == cardis::CardKind::CHARACTER;
        const bool legal = (empty == placement) && CanTarget(game, selection, target, row).accepted;
        Panel(bounds, row == cardis::Row::FRONT ? Color{34, 32, 47, 255} : Color{24, 29, 42, 255});
        const bool selected = !empty && selection.unit == units[column]->id;
        const bool ready = !empty && owner == game.state().priority && CanAttackWith(game, units[column]->id);
        const bool movable =
            !empty && owner == game.state().priority && game.canMove(owner, units[column]->id).accepted;
        const bool focused = selection.hand >= 0 || selection.unit != 0;
        const float breathe = Runtime().motion ? fx::Pulse(effects.time(), 2.8F) : 0.0F;
        if (legal || selected || (!focused && (ready || movable))) {
            const Color edge = selected ? ACCENT_PINK : CYAN;
            DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, selected ? 2.0F : 1.0F,
                                        selected ? edge : Fade(edge, 0.45F + 0.35F * breathe));
        }
        if (empty) {
            type.centered(row == cardis::Row::FRONT ? "前排 · 空位" : "后排 · 空位", bounds.x + bounds.width * 0.5F,
                          bounds.y + 30, 18, legal ? CYAN : MUTED_DIM);
        } else {
            const auto& unit = *units[column];
            const auto& card = game.cards()[unit.card];
            const int health = std::max(0, card.health - unit.damage);
            const float enter = effects.unitEnter(unit.id);
            const float hit = effects.unitHit(unit.id);
            const float guard = effects.unitShield(unit.id);
            // Entrance rises and fades; never from nothing, so the arrival
            // reads as a card being placed rather than a pop.
            const float rise = Runtime().motion ? (1.0F - enter) * 12.0F : 0.0F;
            const Rectangle card_bounds{bounds.x, bounds.y + rise, bounds.width, bounds.height};
            const float alpha = enter;
            effects.placeUnit(unit.id, card_bounds, card.name, accent);
            if (selected || (!focused && (ready || movable))) {
                DrawRectangleRounded(card_bounds, 0.08F, 12, Fade(accent, selected ? 0.10F : 0.05F));
            }
            if (hit > 0.0F) {
                DrawRectangleRounded(card_bounds, 0.08F, 12, Fade(DANGER, hit * 0.34F));
            }
            if (guard > 0.0F) {
                DrawRectangleRounded(card_bounds, 0.08F, 12, Fade(CREAM, guard * 0.16F));
            }
            const bool chipped = card.guard || card.haste;
            type.fit(card.name, card_bounds.x + 13, card_bounds.y + 7, chipped ? 150.0F : 250.0F, 18,
                     selected ? ACCENT_PINK : INK, alpha);
            float chip_x = card_bounds.x + 263.0F;
            if (card.haste) {
                chip_x -= 60.0F;
                Chip({chip_x, card_bounds.y + 5, 56, 22}, "疾奏", type, CREAM, false, alpha);
            }
            if (card.guard) {
                chip_x -= 60.0F;
                Chip({chip_x, card_bounds.y + 5, 56, 22}, "守护", type, CREAM, false, alpha);
            }
            const std::string condition = unit.exhausted                           ? "横置"
                                          : unit.summoned_turn == game.state().turn ? "登场"
                                                                                    : "就绪";
            const Color condition_color = unit.exhausted ? MUTED_DIM
                                           : ready || legal ? CYAN
                                                            : MUTED;
            type.fit(std::to_string(card.attack) + " 攻 / " + std::to_string(health) + " 血", card_bounds.x + 13,
                     card_bounds.y + 42, 118, 18, hit > 0.0F ? DANGER : (ready || legal ? CYAN : MUTED), alpha);
            if (unit.shield > 0) {
                Chip({card_bounds.x + 138, card_bounds.y + 39, 56, 22}, "护 " + std::to_string(unit.shield), type,
                     CREAM, false, alpha);
            }
            type.right(condition, card_bounds.x + 263, card_bounds.y + 42, 18, condition_color, alpha);
            // Health bar: eased so the change is legible, numbers stay still.
            const Rectangle health_bar{card_bounds.x + 13, card_bounds.y + 70, 250, 7};
            const float health_target =
                card.health > 0 ? static_cast<float>(health) / static_cast<float>(card.health) : 0.0F;
            const float health_shown = fx::Saturate(effects.unitLife(unit.id, health_target));
            DrawRectangleRounded(health_bar, 0.5F, 6, Fade(Color{9, 9, 16, 255}, 0.9F * alpha));
            const float lost = std::max(health_shown, health_target);
            if (lost > 0.01F) {
                DrawRectangleRounded({health_bar.x, health_bar.y, health_bar.width * lost, health_bar.height}, 0.5F, 6,
                                     Fade(DANGER, 0.5F * alpha));
            }
            if (health_target > 0.01F) {
                DrawRectangleRounded(
                    {health_bar.x, health_bar.y, health_bar.width * health_target, health_bar.height}, 0.5F, 6,
                    Fade(unit.exhausted ? MUTED : accent, 0.85F * alpha));
            }
        }
        if (focused && !selected && !legal) {
            DrawRectangleRounded(bounds, 0.08F, 12, Fade(BACKGROUND, 0.22F));
        }
        if (Clicked(bounds, interactive)) {
            if (selection.hand >= 0 || (selection.unit != 0 && owner != game.state().priority)) {
                if (!empty || placement) {
                    click = {true, target, row};
                }
            } else if (!empty && (ready || movable)) {
                selection.hand = -1;
                selection.unit = units[column]->id;
                message = ready ? "选择敌方目标攻击。" : "点击右侧按钮换排。";
            }
        }
    }
}
// Hand motion is per column: the lift retargets as the pointer moves, the
// entrance stagger restarts when the page or the hand changes.
struct HandMotion {
    std::array<fx::Transition, 4> lift{fx::Transition{}, fx::Transition{}, fx::Transition{}, fx::Transition{}};
    std::array<fx::Clock, 4> enter{};
    int hovered = -1;
    std::size_t hand_size = 0;
    int page = -1;
};
HandMotion& Hand() {
    static HandMotion motion;
    return motion;
}

void DrawHand(const Typography& type, const cardis::Game& game, Selection& selection, int& page, bool interactive,
              std::string& message, const fx::Director& effects, double frame_delta) {
    const auto player_id = game.state().priority;
    const auto& player = game.state().players[cardis::Index(player_id)];
    const int pages = std::max(1, static_cast<int>((player.hand.size() + 3) / 4));
    page = std::clamp(page, 0, pages - 1);
    auto& hand = Hand();
    if (hand.page != page || hand.hand_size != player.hand.size()) {
        for (auto& clock : hand.enter) {
            clock.start(fx::dur::kEnter);
        }
        hand.page = page;
        hand.hand_size = player.hand.size();
    }
    const float delta = static_cast<float>(frame_delta);
    for (auto& clock : hand.enter) {
        clock.update(delta);
    }
    type.text("P" + std::to_string(cardis::Index(player_id) + 1) + " 手牌 · " + std::to_string(player.hand.size()), 284,
              672, 20, ACCENT_PINK);
    type.fit(message, 517, 675, 554, 18, MUTED);
    if (Button(type, {1113, 669, 69, 33}, "←", false, page > 0)) {
        --page;
    }
    type.text(std::to_string(page + 1) + "/" + std::to_string(pages), 1196, 674, 18, MUTED);
    if (Button(type, {1293, 669, 69, 33}, "→", false, page < pages - 1)) {
        ++page;
    }
    const auto start = static_cast<std::size_t>(page) * 4;
    const bool focused = selection.hand >= 0 || selection.unit != 0;
    int hovered = -1;
    for (std::size_t i = start; i < std::min(start + 4, player.hand.size()); ++i) {
        const auto& card = game.cards()[player.hand[i]];
        const std::size_t column = i - start;
        const Rectangle base = ui::HandCard(static_cast<int>(column));
        const bool selected = selection.hand == static_cast<int>(i);
        const bool available = CanPlayCard(game, i);
        // Hover is tested against the resting rect so the pointer cannot fall
        // out of the target it just raised.
        const bool pointer = interactive && CheckCollisionPointRec(GetMousePosition(), base);
        if (pointer) {
            hovered = static_cast<int>(i);
        }
        if (Runtime().motion) {
            hand.lift[column].retarget(pointer ? 1.0F : 0.0F);
            hand.lift[column].update(delta);
        } else {
            hand.lift[column].snap(pointer ? 1.0F : 0.0F);
        }
        const float enter = hand.enter[column].completed() ? 1.0F : hand.enter[column].eased();
        const float drop = Runtime().motion ? (1.0F - enter) * 16.0F : 0.0F;
        const float offset = -ui::kHandLift * hand.lift[column].value() + drop;
        const Rectangle bounds{base.x, base.y + offset, base.width, base.height};
        const Color accent = card.kind == cardis::CardKind::CHARACTER ? ACCENT_PINK : CYAN;
        Panel(bounds, selected ? Color{55, 39, 57, 255} : PANEL, enter);
        DrawRectangleRounded({bounds.x + 1, bounds.y + 12, 3, bounds.height - 24}, 0.5F, 6,
                             Fade(accent, (available ? 0.85F : 0.3F) * enter));
        const float breathe = Runtime().motion ? fx::Pulse(effects.time(), 2.4F) : 0.0F;
        if (selected || (!focused && available)) {
            DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, selected ? 2.0F : 1.0F,
                                        selected ? ACCENT_PINK : Fade(CYAN, 0.4F + 0.35F * breathe));
        }
        type.fit(card.name, bounds.x + 16, bounds.y + 9, 202, 20, available ? INK : MUTED, enter);
        DrawCircle(static_cast<int>(bounds.x + 242), static_cast<int>(bounds.y + 23), 20,
                   Fade(available ? accent : BORDER, enter));
        type.text(std::to_string(card.cost), bounds.x + 234, bounds.y + 6, 21, BACKGROUND, enter);
        if (card.kind == cardis::CardKind::CHARACTER) {
            type.fit("角色 · " + std::to_string(card.attack) + "攻 / " + std::to_string(card.health) + "血",
                     bounds.x + 16, bounds.y + 53, 244, 18, available ? CYAN : MUTED, enter);
            float chip_x = bounds.x + 16;
            if (card.guard) {
                Chip({chip_x, bounds.y + 85, 56, 22}, "守护", type, CREAM, false, enter);
                chip_x += 62.0F;
            }
            if (card.haste) {
                Chip({chip_x, bounds.y + 85, 56, 22}, "疾奏", type, CREAM, false, enter);
                chip_x += 62.0F;
            }
            type.fit("前后排可放置", chip_x, bounds.y + 88, 244.0F - (chip_x - bounds.x - 16.0F), 18, MUTED, enter);
        } else {
            type.fit("当前：" + TimedEffects(card, cardis::EffectTiming::ON_RESOLVE), bounds.x + 16, bounds.y + 53, 244,
                     18, available ? INK : MUTED, enter);
            type.fit("回合末：" + TimedEffects(card, cardis::EffectTiming::END_OF_TURN), bounds.x + 16, bounds.y + 89,
                     244, 18, MUTED, enter);
        }
        if (card.kind == cardis::CardKind::CHARACTER) {
            type.fit(selected ? "已选择 · 点击空位" : available ? "点击选择" : "暂不可用", bounds.x + 16,
                     bounds.y + 126, 244, 18, selected ? ACCENT_PINK : MUTED, enter);
        } else {
            type.fit(selected    ? "已选择 · 点击目标"
                     : available ? "点击选择"
                                 : "暂不可用",
                     bounds.x + 16, bounds.y + 126, 244, 18, selected ? ACCENT_PINK : MUTED, enter);
        }
        if (focused && !selected) {
            DrawRectangleRounded(bounds, 0.08F, 12, Fade(BACKGROUND, 0.24F));
        }
        if (!available && interactive && pointer) {
            message = player.mana < card.cost ? "灵力不足，等待自己的下回合恢复。"
                      : card.kind == cardis::CardKind::CHARACTER
                          ? MessageLabel(game.canCast(player_id, i, cardis::Target{player_id, 0}).error)
                          : "当前没有合法目标。";
        }
        if (Clicked(base, interactive && available)) {
            selection.unit = 0;
            selection.hand = selected ? -1 : static_cast<int>(i);
            message = card.kind == cardis::CardKind::CHARACTER ? "点击己方前排或后排的空位。" : EffectDescription(card);
        }
    }
    if (player.hand.empty()) {
        type.text("手牌已用尽。下回合开始时抽牌。", 309, 770, 23, MUTED);
    }
    // The console pane reads this on the next frame to show the full card
    // text: hovering wins, otherwise the current selection.
    hand.hovered = hovered;
}
std::string MessageLabel(std::string label) {
    static const std::map<std::string, std::string> ERRORS{
        {"The game has ended", "对局已经结束。"},
        {"Player does not have priority or target is invalid", "当前无优先权，或目标无效。"},
        {"Card is not in hand", "这张卡已不在手牌中。"},
        {"Not enough mana", "灵力不足。"},
        {"Characters require your main phase, an empty stack and your own side", "己方主要阶段空栈时登场。"},
        {"That row is full", "这一排已经满员。"},
        {"That named character is already on your battlefield", "己方已有同名角色。"},
        {"This skill cannot target that side", "该技能不能选择这一方。"},
        {"Attacks require your combat phase, priority and an empty stack", "攻击限己方战斗阶段、空栈时。"},
        {"That unit cannot attack", "该角色当前无法攻击。"},
        {"New units must wait; Haste can attack units on entry", "新角色需等待；疾奏可先打角色。"},
        {"Choose an enemy front-row guard first, then other front-row units", "先打前排守护，再清空前排。"},
        {"Moving requires your main phase, priority and an empty stack", "换排限己方主要阶段、空栈时。"},
        {"Move one ready unit per turn after its summoning turn", "每回合限换排一次，新登场不可。"},
        {"The destination row is full", "目标排已经满员。"},
        {"Player cannot pass now", "当前不能让过优先权。"},
        {"Draw: both heroes were defeated", "双方生命同时归零，平局。"},
    };
    if (const auto found = ERRORS.find(label); found != ERRORS.end()) {
        return found->second;
    }
    const auto replace = [&label](const std::string& from, const std::string& to) {
        const auto position = label.find(from);
        if (position != std::string::npos) {
            label.replace(position, from.size(), to);
        }
    };
    replace(" scheduled an end-of-turn effect", " 已登记回合末效果");
    replace(" end-of-turn effect entered the stack", " 回合末效果已入栈");
    replace(" for turn ", "，触发回合 ");
    replace(" entered the stack", " 已入栈");
    replace(" declared an attack", " 宣告攻击");
    replace(" moved and became exhausted", " 换排并横置");
    replace(" entered the graveyard", " 进入墓地");
    replace(" fizzled: source or target is no longer legal", " 落空：来源或目标已失效");
    replace(" could not draw from an empty deck", " 无牌可抽，落败");
    replace(" won by defeating the enemy hero", " 击败对方玩家，获胜");
    replace(" drew a card", " 抽牌");
    replace(" resolved", " 已结算");
    replace("Player ", "玩家 ");
    replace(" passed", " 让过优先权");
    replace(" lost", " 已落败");
    replace("Shield prevented ", "护盾抵消了 ");
    replace(" damage", " 点伤害");
    replace("Phase advanced", "进入下一阶段");
    return label;
}
std::string TargetLabel(const cardis::Game& game, cardis::Target target) {
    const std::string owner = "P" + std::to_string(cardis::Index(target.player) + 1);
    if (target.unit == 0) {
        return owner + " 玩家";
    }
    for (const auto& unit : game.state().players[cardis::Index(target.player)].battlefield) {
        if (unit.id == target.unit) {
            return owner + " · " + game.cards()[unit.card].name;
        }
    }
    return owner + " · 目标已离场 #" + std::to_string(target.unit);
}
void DrawTimeline(const Typography& type, const cardis::Game& game, bool& open, int& tab, int& page, float reveal) {
    if (reveal <= 0.0F) {
        return;
    }
    const auto& state = game.state();
    DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BLACK, 0.85F * Reveal(reveal, 0, 0.04F)));
    const float panel_alpha = Reveal(reveal, 0, 0.04F);
    Panel({170, 94, 1100, 712}, PANEL, panel_alpha);
    DrawRectangleGradientH(170, 94, 1100, 4, Fade(ACCENT_PINK, 0.8F * panel_alpha),
                           Fade(CYAN, 0.8F * panel_alpha));
    type.text("结算与记录", 205, 114, 28, INK, Reveal(reveal, 1));
    type.fit("响应栈、延迟触发与完整对局日志", 400, 122, 400, 18, MUTED, Reveal(reveal, 1));
    if (Button(type, {1110, 112, 130, 40}, "关闭", false, true, Reveal(reveal, 1))) {
        open = false;
    }
    const std::array<std::string, 3> labels{"当前响应栈", "回合末待触发", "对局记录"};
    for (int i = 0; i < 3; ++i) {
        if (Button(type, {204 + static_cast<float>(i) * 348, 173, 324, 43}, labels[static_cast<std::size_t>(i)],
                   tab == i, true, Reveal(reveal, 2))) {
            tab = i;
            page = 0;
        }
    }
    const std::size_t count = tab == 0   ? state.stack.size()
                              : tab == 1 ? state.scheduled_effects.size()
                                         : state.events.size();
    const int per_page = tab == 2 ? 6 : 2;
    const int pages = std::max(1, (static_cast<int>(count) + per_page - 1) / per_page);
    if (CheckCollisionPointRec(GetMousePosition(), {170, 220, 1100, 486})) {
        page -= static_cast<int>(GetMouseWheelMove());
    }
    page = std::clamp(page, 0, pages - 1);
    const auto begin = static_cast<std::size_t>(page * per_page);
    for (std::size_t index = begin; index < std::min(count, begin + static_cast<std::size_t>(per_page)); ++index) {
        const std::size_t row = index - begin;
        const float y = 242 + static_cast<float>(row) * (tab == 2 ? 74.0F : 216.0F);
        const float alpha = Reveal(reveal, 3 + static_cast<int>(row), 0.05F);
        if (tab == 2) {
            const auto& event = state.events[count - index - 1];
            type.wrapped("#" + std::to_string(event.sequence) + "  " + MessageLabel(event.message), 208, y, 1020, 2, 18,
                         MUTED, alpha);
            continue;
        }
        const auto card_index = tab == 0 ? state.stack[count - index - 1].card : state.scheduled_effects[index].card;
        const auto& card = game.cards()[card_index];
        const auto controller =
            tab == 0 ? state.stack[count - index - 1].controller : state.scheduled_effects[index].controller;
        const auto target = tab == 0 ? state.stack[count - index - 1].target : state.scheduled_effects[index].target;
        const std::string due =
            tab == 1 ? " · 第 " + std::to_string(state.scheduled_effects[index].due_turn) + " 回合末" : "";
        Panel({201, y - 3, 1038, 199}, PANEL_RAISED, alpha);
        DrawRectangleRounded({201, y + 5, 3, 183}, 0.5F, 6, Fade(PlayerAccent(controller), 0.7F * alpha));
        type.fit(std::to_string(index + 1) + ". " + card.name + " · P" + std::to_string(cardis::Index(controller) + 1) +
                     " 施放" + due,
                 215, y + 7, 1006, 20, INK, alpha);
        type.fit("目标：" + TargetLabel(game, target), 215, y + 48, 1006, 18, CYAN, alpha);
        std::string description;
        if (tab == 1) {
            description = "回合末触发：" + StepText(state.scheduled_effects[index].effect);
        } else {
            const auto& item = state.stack[count - index - 1];
            if (item.kind == cardis::StackKind::ATTACK) {
                description = "攻击 · 双方确认后进行攻击结算。";
            } else if (item.kind == cardis::StackKind::TRIGGER) {
                description = "回合末触发 · 可响应：" + StepText(item.triggered_effect);
            } else if (card.kind == cardis::CardKind::CHARACTER) {
                description = std::string("角色登场 · ") + (item.row == cardis::Row::FRONT ? "前排" : "后排") + " · " +
                              std::to_string(card.attack) + " 攻击 / " + std::to_string(card.health) + " 生命";
            } else {
                description = EffectDescription(card);
            }
        }
        type.wrapped(description, 215, y + 93, 1006, 3, 18, MUTED, alpha);
    }
    if (count == 0) {
        type.text(tab == 0   ? "当前没有待响应的结算。"
                  : tab == 1 ? "当前没有登记的回合末效果。"
                             : "尚无对局记录。",
                  210, 294, 23, MUTED, Reveal(reveal, 3));
    }
    if (Button(type, {204, 733, 144, 41}, "上一页", false, page > 0, Reveal(reveal, 3))) {
        --page;
    }
    type.fit(std::to_string(page + 1) + " / " + std::to_string(pages) + " 页 · 共 " + std::to_string(count) + " 项",
             466, 740, 575, 18, MUTED, Reveal(reveal, 3));
    if (Button(type, {1096, 733, 144, 41}, "下一页", false, page < pages - 1, Reveal(reveal, 3))) {
        ++page;
    }
}
// Phase stepper and priority readout above the action buttons.
void DrawPhaseHeader(const Typography& type, const cardis::Game& game, const fx::Director& effects) {
    const auto& state = game.state();
    const Rectangle pane = ui::PhasePane();
    const Color accent = PlayerAccent(state.priority);
    Panel(pane);
    DrawRectangleGradientH(static_cast<int>(pane.x), static_cast<int>(pane.y), static_cast<int>(pane.width), 3,
                           Fade(accent, 0.8F), Fade(BLANK, 0.0F));
    const cardis::Phase order[3]{cardis::Phase::MAIN, cardis::Phase::COMBAT, cardis::Phase::END};
    const char* names[3]{"主要", "战斗", "结束"};
    const int current = static_cast<int>(state.phase);
    for (int step = 0; step < 3; ++step) {
        const Rectangle bounds{pane.x + 14 + static_cast<float>(step) * 78.0F, pane.y + 14, 74, 26};
        const bool active = state.phase == order[step];
        const bool past = current > step;
        Chip(bounds, names[step], type, active ? accent : past ? MUTED : BORDER_SOFT, active);
        if (step < 2) {
            DrawRectangle(static_cast<int>(bounds.x + bounds.width + 2), static_cast<int>(bounds.y + 12), 2, 1,
                          Fade(past ? accent : BORDER_SOFT, 0.8F));
        }
    }
    const float breathe = Runtime().motion ? fx::Pulse(effects.time(), 2.6F) : 0.0F;
    type.fit("P" + std::to_string(cardis::Index(state.priority) + 1) + " 持有优先权", pane.x + 14, pane.y + 46, 200,
             18, MUTED);
    DrawCircleV({pane.x + 186, pane.y + 55}, 5, Fade(state.consecutive_passes >= 1 ? accent : BORDER, 0.85F));
    DrawCircleV({pane.x + 208, pane.y + 55}, 5,
                Fade(state.consecutive_passes >= 2 ? accent : BORDER, 0.85F));
    const std::string hint = state.phase == cardis::Phase::MAIN     ? "召唤角色与施放技能"
                             : state.phase == cardis::Phase::COMBAT ? "宣告攻击，双方响应"
                             : state.phase == cardis::Phase::END    ? "结算延迟效果"
                                                                    : "对局结束";
    type.fit(hint, pane.x + 14, pane.y + 70, 234, 18, MUTED_DIM);
    if (Runtime().motion && state.consecutive_passes > 0) {
        DrawRectangleRoundedLinesEx(pane, 0.08F, 12, 1, Fade(accent, 0.25F + 0.25F * breathe));
    }
}

void DrawSidebar(const Typography& type, const cardis::Game& game, const fx::Director& effects) {
    const auto& state = game.state();
    const Rectangle stack_pane = ui::StackPane();
    effects.placeStack(stack_pane);
    Panel(stack_pane);
    const float arrival = Runtime().motion ? effects.stackArrival() : 0.0F;
    if (arrival > 0.0F) {
        DrawRectangleRoundedLinesEx(stack_pane, 0.08F, 12, 2, Fade(ACCENT_PINK, 0.75F * arrival));
    }
    type.fit("当前响应栈 · " + std::to_string(state.stack.size()), stack_pane.x + 14, stack_pane.y + 8, 234, 18,
             ACCENT_PINK);
    if (state.stack.empty()) {
        type.text("等待出牌或攻击", stack_pane.x + 14, stack_pane.y + 66, 18, MUTED);
        type.wrapped("攻击与技能都要等双方让过后才结算。", stack_pane.x + 14, stack_pane.y + 108, 234, 3, 18,
                     MUTED_DIM);
        return;
    }
    int index = 0;
    for (auto it = state.stack.rbegin(); it != state.stack.rend() && index < 2; ++it, ++index) {
        const float y = stack_pane.y + 42 + static_cast<float>(index) * 68.0F;
        const auto& card = game.cards()[it->card];
        const Color accent = PlayerAccent(it->controller);
        DrawRectangleRounded({stack_pane.x + 12, y, 238, 62}, 0.1F, 10,
                             Fade(PANEL_DEEP, index == 0 ? 0.95F : 0.62F));
        DrawRectangleRounded({stack_pane.x + 12, y + 9, 3, 44}, 0.5F, 6, Fade(accent, index == 0 ? 0.9F : 0.4F));
        type.fit(std::to_string(index + 1) + ". " + card.name, stack_pane.x + 26, y + 7, 212, 18,
                 index == 0 ? INK : MUTED);
        const std::string kind = it->kind == cardis::StackKind::ATTACK    ? "攻击"
                                 : it->kind == cardis::StackKind::TRIGGER ? "触发"
                                                                          : "施放";
        type.fit(kind + " → " + TargetLabel(game, it->target), stack_pane.x + 26, y + 35, 212, 18, MUTED_DIM);
    }

    const Rectangle scheduled_pane = ui::ScheduledPane();
    Panel(scheduled_pane);
    type.fit("回合末待触发 · " + std::to_string(state.scheduled_effects.size()), scheduled_pane.x + 14,
             scheduled_pane.y + 10, 234, 18, CREAM);
    if (state.scheduled_effects.empty()) {
        type.text("暂无延迟效果", scheduled_pane.x + 14, scheduled_pane.y + 56, 18, MUTED);
        type.wrapped("攻击在当前响应栈结算。", scheduled_pane.x + 14, scheduled_pane.y + 100, 234, 2, 18, MUTED_DIM);
        return;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(2, state.scheduled_effects.size()); ++index) {
        const auto& scheduled = state.scheduled_effects[index];
        const float y = scheduled_pane.y + 42 + static_cast<float>(index) * 68.0F;
        DrawRectangleRounded({scheduled_pane.x + 12, y, 238, 62}, 0.1F, 10, Fade(PANEL_DEEP, 0.9F));
        DrawRectangleRounded({scheduled_pane.x + 12, y + 9, 3, 44}, 0.5F, 6,
                             Fade(PlayerAccent(scheduled.controller), 0.7F));
        type.fit(game.cards()[scheduled.card].name, scheduled_pane.x + 26, y + 7, 212, 18, INK);
        type.fit(TargetLabel(game, scheduled.target), scheduled_pane.x + 26, y + 33, 148, 18, MUTED_DIM);
        Chip({scheduled_pane.x + 180, y + 32, 58, 22}, "T" + std::to_string(scheduled.due_turn) + " 末", type, CREAM,
             false, 1.0F, 18);
    }
}
// The board's centre gap, used as a live stack rail: what is waiting to
// resolve, in order, right where both players are already looking.
void DrawRail(const Typography& type, const cardis::Game& game, const fx::Director& effects) {
    const auto& state = game.state();
    const Rectangle rail = ui::Rail();
    effects.placeRail(rail);
    const float middle = rail.y + rail.height * 0.5F;
    DrawRectangleRounded(rail, 0.6F, 8, Fade(PANEL_DEEP, 0.7F));
    DrawLineEx({rail.x + 14, middle}, {rail.x + rail.width - 14, middle}, 1, Fade(BORDER, 0.7F));
    if (state.stack.empty()) {
        type.centered("空栈 · " + PhaseName(state.phase), rail.x + rail.width * 0.5F, rail.y + 1, 18, MUTED_DIM);
        return;
    }
    const float arrival = Runtime().motion ? effects.stackArrival() : 0.0F;
    float x = rail.x + 14;
    const std::string label = "结算栈 " + std::to_string(state.stack.size());
    type.text(label, x, rail.y + 2, 18, ACCENT_PINK);
    x += type.width(label, 18) + 16.0F;
    int index = 0;
    for (auto it = state.stack.rbegin(); it != state.stack.rend() && index < 3; ++it, ++index) {
        const auto& card = game.cards()[it->card];
        const Color accent = PlayerAccent(it->controller);
        const std::string kind = it->kind == cardis::StackKind::ATTACK    ? "攻击"
                                 : it->kind == cardis::StackKind::TRIGGER ? "触发"
                                                                          : "施放";
        const std::string chip = kind + " " + card.name;
        const float width = type.width(chip, 18) + 22.0F;
        if (x + width > rail.x + rail.width - 14) {
            type.text("+" + std::to_string(static_cast<int>(state.stack.size()) - index), x, rail.y + 2, 18, MUTED_DIM);
            break;
        }
        const float highlight = index == 0 ? 0.20F + 0.18F * arrival : 0.10F;
        DrawRectangleRounded({x, rail.y + 1, width, 19}, 0.5F, 8, Fade(accent, highlight));
        DrawRectangleRoundedLinesEx({x, rail.y + 1, width, 19}, 0.5F, 8, 1, Fade(accent, index == 0 ? 0.95F : 0.45F));
        type.text(chip, x + 11, rail.y + 2, 18, index == 0 ? INK : MUTED);
        x += width + 8.0F;
    }
}

void SeedBattleDemo(cardis::Game& game, bool leave_stack) {
    // Exercise public commands rather than mutating the game for screenshots.
    for (int step = 0; step < 180 && game.state().phase != cardis::Phase::FINISHED; ++step) {
        const auto& state = game.state();
        if (state.turn >= 7 && !state.players[0].battlefield.empty() && !state.players[1].battlefield.empty()) {
            if (!leave_stack || !state.stack.empty()) {
                return;
            }
        }
        bool cast = false;
        if (state.phase == cardis::Phase::MAIN && state.stack.empty() && state.active == state.priority) {
            const auto player = state.priority;
            const auto& hand = state.players[cardis::Index(player)].hand;
            for (std::size_t i = 0; i < hand.size(); ++i) {
                if (game.cards()[hand[i]].kind != cardis::CardKind::CHARACTER) {
                    continue;
                }
                for (const auto row : {cardis::Row::FRONT, cardis::Row::BACK}) {
                    if (game.canCast(player, i, {player, 0}, row).accepted) {
                        const auto result = game.cast(player, i, {player, 0}, row);
                        if (!result.accepted) {
                            throw std::runtime_error(result.error);
                        }
                        cast = true;
                        break;
                    }
                }
                if (cast) {
                    break;
                }
            }
        }
        if (!cast) {
            const auto result = game.pass(game.state().priority);
            if (!result.accepted) {
                throw std::runtime_error(result.error);
            }
        }
    }
}
void SeedDelayedDemo(cardis::Game& game) {
    for (int step = 0; step < 360 && game.state().phase != cardis::Phase::FINISHED; ++step) {
        const auto player = game.state().priority;
        const auto& hand = game.state().players[cardis::Index(player)].hand;
        if (game.state().stack.empty() && game.state().phase != cardis::Phase::END) {
            for (std::size_t i = 0; i < hand.size(); ++i) {
                const auto effects = cardis::EffectsOf(game.cards()[hand[i]]);
                if (std::none_of(effects.begin(), effects.end(), [](const auto& effect) {
                        return effect.timing == cardis::EffectTiming::END_OF_TURN;
                    })) {
                    continue;
                }
                for (const auto target : {cardis::PlayerId::FIRST, cardis::PlayerId::SECOND}) {
                    if (!game.canCast(player, i, cardis::Target{target, 0}).accepted) {
                        continue;
                    }
                    const auto result = game.cast(player, i, cardis::Target{target, 0});
                    if (!result.accepted) {
                        throw std::runtime_error(result.error);
                    }
                    for (int pass = 0; pass < 2; ++pass) {
                        if (!game.pass(game.state().priority).accepted) {
                            throw std::runtime_error("Delayed smoke could not pass");
                        }
                    }
                    if (game.state().scheduled_effects.empty()) {
                        throw std::runtime_error("Delayed smoke did not schedule an effect");
                    }
                    return;
                }
            }
        }
        if (!game.pass(player).accepted) {
            throw std::runtime_error("Delayed smoke could not advance");
        }
    }
    throw std::runtime_error("No legal delayed card found for smoke test");
}
// Snapshot of what the effects layer needs to see change: hero pools, unit
// health and shields, stack depth and turn flow.
fx::Snapshot BuildSnapshot(const cardis::Game& game) {
    const auto& state = game.state();
    fx::Snapshot snapshot;
    snapshot.turn = state.turn;
    snapshot.phase = static_cast<int>(state.phase);
    snapshot.active = static_cast<int>(cardis::Index(state.active));
    snapshot.priority = static_cast<int>(cardis::Index(state.priority));
    snapshot.passes = state.consecutive_passes;
    snapshot.finished = state.phase == cardis::Phase::FINISHED;
    snapshot.winner = state.winner ? static_cast<int>(cardis::Index(*state.winner)) : -1;
    snapshot.stack_size = static_cast<int>(state.stack.size());
    snapshot.scheduled = static_cast<int>(state.scheduled_effects.size());
    for (int index = 0; index < 2; ++index) {
        const auto& player = state.players[index];
        auto& entry = snapshot.players[index];
        entry.life = player.life;
        entry.max_life = std::max(1, player.max_life);
        entry.mana = player.mana;
        entry.max_mana = std::max(0, player.max_mana);
        entry.shield = player.shield;
        entry.hand = static_cast<int>(player.hand.size());
        entry.deck = static_cast<int>(player.deck.size());
        entry.graveyard = static_cast<int>(player.graveyard.size());
        for (const auto& unit : player.battlefield) {
            const auto& card = game.cards()[unit.card];
            fx::UnitSnapshot record;
            record.id = unit.id;
            record.life = std::max(0, card.health - unit.damage);
            record.max_life = std::max(1, card.health);
            record.shield = unit.shield;
            record.exhausted = unit.exhausted;
            snapshot.units[index].push_back(record);
        }
    }
    if (!state.stack.empty()) {
        const auto& top = state.stack.back();
        snapshot.top.present = true;
        snapshot.top.kind = static_cast<int>(top.kind);
        snapshot.top.controller = static_cast<int>(cardis::Index(top.controller));
        snapshot.top.attacker = top.attacker;
        snapshot.top.target_player = static_cast<int>(cardis::Index(top.target.player));
        snapshot.top.target_unit = top.target.unit;
    }
    return snapshot;
}

int Run(const std::filesystem::path& manifest, const std::string& mode, const std::string& screenshot) {
    auto runtime = cardis::CreateRuntime(manifest);
    auto& game = runtime->require<cardis::Game>();
    const auto& roster = runtime->require<cardis::CharacterRoster>();
    if (mode == "--smoke-stack" || mode == "--smoke-battle" || mode == "--smoke-small" || mode == "--smoke-focus" ||
        mode == "--smoke-turn") {
        SeedBattleDemo(game, mode == "--smoke-stack");
    }
    if (mode == "--smoke-delayed" || mode == "--smoke-timeline") {
        SeedDelayedDemo(game);
    }
    if (mode == "--smoke-turn") {
        const auto turn = game.state().turn;
        for (int i = 0; i < 20 && game.state().turn == turn; ++i) {
            static_cast<void>(game.pass(game.state().priority));
        }
        if (game.state().turn == turn) {
            throw std::runtime_error("Turn smoke failed to advance");
        }
    }
    if (mode == "--smoke-p2") {
        static_cast<void>(game.pass(game.state().priority));
    }
    const bool smoke = mode.rfind("--smoke", 0) == 0;
    const Window window;
    if (mode == "--smoke-small") {
        SetWindowSize(1000, 700);
    }
    Canvas canvas;
    Typography type(manifest.parent_path());
    Portraits portraits(manifest.parent_path());
    Backdrop backdrop;
    fx::Director effects;
    const auto syncSettings = [&effects]() {
        effects.settings().motion = Runtime().motion;
        effects.settings().particles = Runtime().motion;
        effects.settings().shake = Runtime().motion;
    };
    syncSettings();
    std::string message = "选择手牌，再点击目标或空位。";
    Selection selection;
    int page = 0;
    if (mode == "--smoke-focus") {
        const auto& hand = game.state().players[cardis::Index(game.state().priority)].hand;
        for (std::size_t i = 0; i < hand.size(); ++i) {
            if (CanPlayCard(game, i)) {
                selection.hand = static_cast<int>(i);
                page = static_cast<int>(i / 4);
                break;
            }
        }
        if (selection.hand < 0) {
            throw std::runtime_error("Focus smoke has no legal card");
        }
    }
    int frames = 0;
    auto previous_player = game.state().priority;
    auto inspected = game.state().priority;
    bool details = mode == "--smoke-profile";
    bool timeline = mode == "--smoke-timeline";
    int timeline_tab = timeline ? 1 : 0;
    int timeline_page = 0;
    auto previous_turn = game.state().turn - (mode == "--smoke-turn" ? 1U : 0U);
    auto previous_phase = game.state().phase;
    double handoff_until = -1;
    double phase_until = -1;
    fx::Clock details_reveal;
    fx::Clock timeline_reveal;
    fx::Clock finished_reveal;
    bool details_shown = false;
    bool timeline_shown = false;
    bool finished_shown = false;
    bool handoff_shown = false;
    fx::Clock handoff_enter;
    while (!WindowShouldClose()) {
        // Smoke frames step a fixed clock so screenshots land on settled
        // motion instead of whatever the first frame caught.
        const float delta = smoke ? 0.1F : std::clamp(GetFrameTime(), 0.0F, 0.05F);
        // Smoke frames step a frozen clock forward so screenshots show settled
        // motion and a countdown that has actually moved.
        const double presentation_time = smoke ? 1.0 + static_cast<double>(frames) * 0.1 : GetTime();
        if (game.state().turn != previous_turn) {
            handoff_until = presentation_time + (smoke ? 1.8 : 3.0);
            previous_turn = game.state().turn;
            selection.clear();
            details = false;
            timeline = false;
        }
        if (game.state().phase != previous_phase) {
            phase_until = presentation_time + 0.8;
            previous_phase = game.state().phase;
        }
        if (game.state().phase == cardis::Phase::FINISHED) {
            handoff_until = -1;
        }
        const bool handoff = presentation_time < handoff_until;
        if (handoff && !handoff_shown) {
            handoff_enter.start(0.26F);
        }
        handoff_shown = handoff;
        handoff_enter.update(delta);
        if (previous_player != game.state().priority) {
            selection.clear();
            page = 0;
            inspected = game.state().priority;
            previous_player = game.state().priority;
        }
        if (selection.hand >=
            static_cast<int>(game.state().players[cardis::Index(game.state().priority)].hand.size())) {
            selection.clear();
        }
        if (IsKeyPressed(KEY_F)) {
            Runtime().motion = !Runtime().motion;
            syncSettings();
            message = Runtime().motion ? "特效已开启。" : "特效已关闭，仅保留必要反馈。";
        }
        const auto& state = game.state();
        const bool interactive = !details && !timeline && !handoff && state.phase != cardis::Phase::FINISHED;
        // Effects run before the board is drawn: existing motion advances, the
        // snapshot diff spawns this frame's feedback, and the board supplies
        // fresh placements as it draws.
        effects.advance(delta);
        effects.observe(BuildSnapshot(game));
        const Vector2 shake = effects.shake();
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        canvas.begin(shake);
        type.setViewport(canvas.scale(), canvas.origin());
        backdrop.draw(shake);
        DrawStageGlow(effects, state.active);
        if (details && !details_shown) {
            details_reveal.start(1.0F);
            details_shown = true;
        }
        details_shown = details;
        if (timeline && !timeline_shown) {
            timeline_reveal.start(1.0F);
            timeline_shown = true;
        }
        timeline_shown = timeline;
        const bool finished_now = state.phase == cardis::Phase::FINISHED && !details && !timeline;
        if (finished_now && !finished_shown) {
            finished_reveal.start(1.0F);
        }
        finished_shown = finished_now;
        details_reveal.update(delta);
        timeline_reveal.update(delta);
        finished_reveal.update(delta);
        if (details || timeline || handoff) {
            GuiLock();
        }
        if (interactive && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            selection.clear();
        }
        DrawRectangleGradientH(0, 0, WIDTH, 73, Color{30, 22, 38, 255}, Color{13, 12, 22, 255});
        DrawRectangleGradientH(0, 71, WIDTH, 2, Fade(PlayerAccent(state.priority), 0.55F), Fade(BLANK, 0.0F));
        type.text("C A R D I S", 29, 13, 28, INK);
        type.text("回合 " + std::to_string(state.turn), 31, 46, 18, MUTED_DIM);
        const std::string actor = "P" + std::to_string(cardis::Index(state.priority) + 1);
        const std::string action = state.phase == cardis::Phase::FINISHED ? "对局结束"
                                   : handoff                              ? "回合交接中"
                                   : selection.hand >= 0                  ? "请选择目标 / 放置位置"
                                   : selection.unit != 0                  ? "请选择攻击目标 / 换排"
                                   : !HasAction(game)                     ? "暂无可用行动 · 请让过"
                                   : !state.stack.empty()                 ? "响应窗口 · 可出牌或让过"
                                                                          : "选择高亮手牌或角色";
        const float pulse = static_cast<float>(std::clamp((phase_until - presentation_time) / 0.8, 0.0, 1.0));
        const Color action_accent = PlayerAccent(state.priority);
        Panel(ui::ActionPane(),
              ColorLerp(PANEL_RAISED, action_accent, 0.10F + 0.12F * pulse));
        type.fit(actor + " · " + action, 302, 9, 811, 22, action_accent);
        type.fit("回合 " + std::to_string(state.turn) + " · " + PhaseName(state.phase), 302, 43, 520, 18, MUTED);
        type.text("双方确认", 980, 45, 18, MUTED_DIM);
        for (int pip = 0; pip < 2; ++pip) {
            DrawCircleV({1064 + static_cast<float>(pip) * 26, 54}, 7,
                        Fade(pip < state.consecutive_passes ? action_accent : BORDER, 0.9F));
        }
        const bool restart = Button(type, ui::RestartButton(), "重新开始");
        const auto& hand_cards = state.players[cardis::Index(state.priority)].hand;
        const int preview_index = Hand().hovered >= 0 ? Hand().hovered : selection.hand;
        const cardis::CardDefinition* preview =
            preview_index >= 0 && static_cast<std::size_t>(preview_index) < hand_cards.size()
                ? &game.cards()[hand_cards[static_cast<std::size_t>(preview_index)]]
                : nullptr;
        DrawCharacterPane(type, portraits, roster, inspected, details, timeline, effects, preview);
        if (selection.hand >= 0 || selection.unit != 0) {
            DrawRectangleRounded(ui::CharacterPane(), 0.08F, 12, Fade(BACKGROUND, 0.18F));
        }
        TargetClick target;
        DrawPlayer(type, portraits, game, roster, cardis::PlayerId::SECOND, true, selection, target, interactive,
                   effects);
        DrawRow(type, game, cardis::PlayerId::SECOND, cardis::Row::BACK, 0, selection, target, interactive, message,
                effects);
        DrawRow(type, game, cardis::PlayerId::SECOND, cardis::Row::FRONT, 1, selection, target, interactive, message,
                effects);
        DrawRail(type, game, effects);
        DrawRow(type, game, cardis::PlayerId::FIRST, cardis::Row::FRONT, 2, selection, target, interactive, message,
                effects);
        DrawRow(type, game, cardis::PlayerId::FIRST, cardis::Row::BACK, 3, selection, target, interactive, message,
                effects);
        DrawPlayer(type, portraits, game, roster, cardis::PlayerId::FIRST, false, selection, target, interactive,
                   effects);
        DrawPhaseHeader(type, game, effects);
        const bool pass = Button(type, {1162, 184, 238, 43}, PassLabel(state), true, interactive);
        const bool move =
            Button(type, {1162, 239, 238, 34}, "所选角色换排", false,
                   interactive && selection.unit != 0 && game.canMove(state.priority, selection.unit).accepted);
        DrawSidebar(type, game, effects);
        DrawHand(type, game, selection, page, interactive, message, effects, delta);
        GuiUnlock();
        if (details) {
            DrawCharacterDetails(type, portraits, roster, game, inspected, details, details_reveal.elapsed());
        }
        if (timeline) {
            DrawTimeline(type, game, timeline, timeline_tab, timeline_page, timeline_reveal.elapsed());
        }
        if (handoff) {
            const float total = smoke ? 1.8F : 3.0F;
            const float remain = static_cast<float>(std::max(0.0, handoff_until - presentation_time));
            const float progress = fx::Saturate(1.0F - remain / total);
            const float enter = Runtime().motion ? handoff_enter.eased() : 1.0F;
            const Color accent = PlayerAccent(state.active);
            const float dy = (1.0F - enter) * 10.0F;
            DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BACKGROUND, 0.88F * enter));
            Panel({364, 221, 712, 437}, Color{40, 34, 52, 255}, enter);
            DrawRectangleGradientH(364, 221, 712, 4, Fade(accent, 0.9F * enter), Fade(BLANK, 0.0F));
            const std::string next = "P" + std::to_string(cardis::Index(state.active) + 1);
            type.fit("P" + std::to_string(cardis::Index(cardis::Opponent(state.active)) + 1) + " 回合结束", 406,
                     255 + dy, 626, 26, MUTED, enter);
            type.fit(next + " 准备行动", 406, 312 + dy, 470, 34, accent, enter);
            const auto& next_character =
                roster.character(roster.player_character_ids[cardis::Index(state.active)]);
            const auto portrait = portraits.get(next_character.portrait);
            if (portrait.id != 0) {
                const Rectangle frame_bounds{900, 292, 120, 120};
                const float side = static_cast<float>(std::min(portrait.width, portrait.height)) * 0.62F;
                const Rectangle source{(static_cast<float>(portrait.width) - side) * 0.5F,
                                       static_cast<float>(portrait.height) * 0.03F, side, side};
                DrawRectangleRounded({frame_bounds.x - 3, frame_bounds.y - 3, frame_bounds.width + 6,
                                      frame_bounds.height + 6},
                                     0.14F, 10, Fade(PANEL_DEEP, enter));
                DrawTexturePro(portrait, source, frame_bounds, {0, 0}, 0, Fade(WHITE, enter));
                DrawRectangleRoundedLinesEx({frame_bounds.x - 3, frame_bounds.y - 3, frame_bounds.width + 6,
                                             frame_bounds.height + 6},
                                            0.14F, 10, 2, Fade(accent, 0.85F * enter));
            }
            const Vector2 ring_centre{720.0F, 452.0F};
            DrawRing(ring_centre, 41.0F, 48.0F, 0.0F, 360.0F, 52, Fade(BORDER, 0.9F * enter));
            DrawRing(ring_centre, 41.0F, 48.0F, -90.0F, -90.0F + 360.0F * progress, 52, Fade(accent, 0.95F * enter));
            type.centered(std::to_string(static_cast<int>(fx::Ceil(remain))), ring_centre.x, ring_centre.y - 22.0F, 40,
                          CREAM, enter);
            type.fit("准备好后可跳过，接下来由你行动。", 406, 546 + dy, 626, 18, MUTED, enter);
            if (Button(type, {406, 581, 626, 48}, "准备好了 · 空格跳过", true, true, enter) ||
                IsKeyPressed(KEY_SPACE)) {
                handoff_until = -1;
            }
        }
        if (finished_now) {
            const float reveal = finished_reveal.elapsed();
            const float alpha = Reveal(reveal, 0, 0.04F, 0.24F);
            const Color accent = state.winner ? PlayerAccent(*state.winner) : MUTED;
            DrawRectangle(270, 173, 872, 399, Fade(BLACK, 0.88F * alpha));
            DrawRectangleGradientH(270, 173, 872, 4, Fade(accent, 0.9F * alpha), Fade(BLANK, 0.0F));
            type.centered(state.winner ? "P" + std::to_string(cardis::Index(*state.winner) + 1) + " 获胜" : "平局",
                          706, 279, 40, accent, Reveal(reveal, 1));
            type.fit(MessageLabel(state.result), 374, 359, 673, 22, INK, Reveal(reveal, 2));
            type.centered("点击右上角重新开始，再奏一曲。", 706, 428, 22, MUTED, Reveal(reveal, 3));
        }
        DrawEffectOverlay(type, effects, details || timeline || handoff || finished_now);
        // Defer commands until drawing is complete, preserving every reference used above.
        if (restart) {
            game.reset();
            effects.reset();
            Hand() = HandMotion{};
            details_reveal = fx::Clock{};
            timeline_reveal = fx::Clock{};
            finished_reveal = fx::Clock{};
            previous_turn = game.state().turn;
            previous_phase = game.state().phase;
            handoff_until = -1;
            phase_until = -1;
            selection.clear();
            page = 0;
            message = "新的对局已开始。";
        } else if (target.clicked) {
            const auto result =
                selection.hand >= 0
                    ? game.cast(state.priority, static_cast<std::size_t>(selection.hand), target.target, target.row)
                    : game.attack(state.priority, selection.unit, target.target);
            message = result.accepted ? "已入栈。双方让过后结算。" : MessageLabel(result.error);
            if (result.accepted) {
                selection.clear();
            }
        } else if (pass) {
            const auto result = game.pass(state.priority);
            message = result.accepted ? "优先权已让过。" : MessageLabel(result.error);
            selection.clear();
        } else if (move) {
            const auto result = game.move(state.priority, selection.unit);
            message = result.accepted ? "已换排，本回合横置。" : MessageLabel(result.error);
            selection.clear();
        }
        const bool last_frame = smoke && ++frames >= 4;
        canvas.end(last_frame ? screenshot : "");
        if (last_frame) {
            break;
        }
    }
    return 0;
}
}  // namespace
int main(int argc, char* argv[]) {
    try {
        std::string manifest_argument;
        std::string mode;
        std::string screenshot;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--no-fx" || argument == "--reduced-motion") {
                Runtime().motion = false;
            } else if (manifest_argument.empty()) {
                manifest_argument = argument;
            } else if (mode.empty()) {
                mode = argument;
            } else if (screenshot.empty()) {
                screenshot = argument;
            }
        }
        const auto manifest = manifest_argument.empty()
                                  ? std::filesystem::path(GetApplicationDirectory()) / "assets/cards.json"
                                  : std::filesystem::path(manifest_argument);
        return Run(manifest, mode, screenshot);
    } catch (const std::exception& error) {
        std::cerr << "Cardis: " << error.what() << '\n';
        return 1;
    }
}
