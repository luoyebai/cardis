#include <algorithm>
#include <cmath>
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

namespace {
constexpr int WIDTH = 1440;
constexpr int HEIGHT = 900;
constexpr Color BACKGROUND{17, 18, 29, 255};
constexpr Color PANEL{27, 28, 43, 255};
constexpr Color BORDER{53, 51, 70, 255};
constexpr Color ACCENT_PINK{244, 170, 197, 255};
constexpr Color CYAN{148, 215, 220, 255};
constexpr Color CREAM{246, 229, 192, 255};
constexpr Color INK{246, 240, 246, 255};
constexpr Color MUTED{184, 181, 201, 255};

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
    void begin() {
        scale_ = std::min(static_cast<float>(GetScreenWidth()) / WIDTH, static_cast<float>(GetScreenHeight()) / HEIGHT);
        origin_ = {std::round((GetScreenWidth() - WIDTH * scale_) / 2),
                   std::round((GetScreenHeight() - HEIGHT * scale_) / 2)};
        SetMouseOffset(-static_cast<int>(origin_.x), -static_cast<int>(origin_.y));
        SetMouseScale(1 / scale_, 1 / scale_);
        BeginDrawing();
        ClearBackground(BACKGROUND);
        BeginMode2D({origin_, {0, 0}, 0, scale_});
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
    void text(const std::string& value, float x, float y, float size = 18, Color color = INK) const {
        const auto& font = fontFor(size);
        const float pixel_scale = scale_ * density_;
        const Vector2 aligned{(std::round((x * scale_ + origin_.x) * density_) / density_ - origin_.x) / scale_,
                              (std::round((y * scale_ + origin_.y) * density_) / density_ - origin_.y) / scale_};
        // Font atlas texels map 1:1 to framebuffer pixels, even at fractional window scales.
        DrawTextEx(font, value.c_str(), aligned, static_cast<float>(font.baseSize) / pixel_scale, 0, color);
    }
    void fit(const std::string& value, float x, float y, float width, float size = 18, Color color = INK) const {
        if (measure(value, size) <= width) {
            text(value, x, y, size, color);
            return;
        }
        std::string clipped = value;
        while (!clipped.empty() && measure(clipped + "…", size) > width) {
            popCharacter(clipped);
        }
        text(clipped + "…", x, y, size, color);
    }
    void wrapped(const std::string& value, float x, float y, float width, int max_lines, float size = 18,
                 Color color = INK) const {
        std::string line;
        int row = 0;
        const float line_height = static_cast<float>(fontFor(size).baseSize) / (scale_ * density_) + 3;
        for (std::size_t offset = 0; offset < value.size();) {
            int bytes = 0;
            static_cast<void>(GetCodepointNext(value.c_str() + offset, &bytes));
            const auto next = value.substr(offset, static_cast<std::size_t>(bytes));
            if (!line.empty() && measure(line + next, size) > width) {
                if (row == max_lines - 1) {
                    fit(line + value.substr(offset), x, y + row * line_height, width, size, color);
                    return;
                }
                text(line, x, y + row * line_height, size, color);
                ++row;
                line.clear();
            }
            line += next;
            offset += static_cast<std::size_t>(bytes);
        }
        if (!line.empty()) {
            text(line, x, y + row * line_height, size, color);
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

void Panel(Rectangle bounds, Color color = PANEL) {
    DrawRectangleRounded(bounds, 0.08F, 12, color);
    DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 1, BORDER);
}

bool Button(const Typography& type, Rectangle bounds, const std::string& label, bool primary = false,
            bool enabled = true) {
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    if (hovered) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }
    const Color fill = !enabled  ? Color{37, 37, 51, 255}
                       : primary ? (hovered ? CREAM : ACCENT_PINK)
                                 : (hovered ? BORDER : PANEL);
    DrawRectangleRounded(bounds, 0.2F, 12, fill);
    if (!primary) {
        DrawRectangleRoundedLinesEx(bounds, 0.2F, 12, 1, hovered ? ACCENT_PINK : BORDER);
    }
    type.fit(label, bounds.x + 18, bounds.y + (bounds.height - 19) / 2, bounds.width - 36, 19,
             !enabled  ? MUTED
             : primary ? BACKGROUND
                       : INK);
    // raygui owns control hit testing; drawing above supplies the game's visual style.
    GuiSetAlpha(0);
    if (!enabled) {
        GuiDisable();
    }
    const bool pressed = GuiButton(bounds, "") != 0;
    GuiEnable();
    GuiSetAlpha(1);
    return pressed && enabled;
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
                          const cardis::Game& game, cardis::PlayerId inspected, bool& open) {
    const auto& character = roster.character(roster.player_character_ids[cardis::Index(inspected)]);
    DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BLACK, 0.78F));
    Panel({170, 94, 1100, 712});
    type.text(character.name, 210, 120, 34);
    type.fit(character.original_name + " / " + character.title, 210, 178, 320, 18, ACCENT_PINK);
    const auto portrait = portraits.get(character.portrait);
    if (portrait.id != 0) {
        const float scale = std::min(300.0F / portrait.width, 535.0F / portrait.height);
        DrawTexturePro(portrait, {0, 0, static_cast<float>(portrait.width), static_cast<float>(portrait.height)},
                       {215, 242, portrait.width * scale, portrait.height * scale}, {0, 0}, 0, WHITE);
    }
    if (Button(type, {1110, 116, 132, 40}, "关闭 ×")) {
        open = false;
    }
    type.text("作品资料", 560, 127, 23, ACCENT_PINK);
    type.fit("作品  " + character.origin, 560, 180, 650, 18);
    type.fit("组别  " + roster.group(character.group_id).name + "  ·  " + GenderLabel(character.gender) + "  ·  " +
                 character.species,
             560, 216, 650, 18);
    type.fit("身份  " + character.occupation, 560, 252, 650, 18);
    type.wrapped(character.biography, 560, 292, 646, 2, 18, MUTED);
    type.text("游戏设定", 560, 373, 23, CYAN);
    type.fit(character.combat_role + "  /  初始生命 " + std::to_string(character.starting_life) + "  /  法力上限 " +
                 std::to_string(character.max_mana),
             560, 418, 650, 18);
    std::string tags = "标签  ";
    for (const auto& tag : character.tags) {
        tags += tag + "   ";
    }
    type.fit(tags, 560, 454, 650, 18, MUTED);
    float y = 502;
    for (const auto& skill_id : character.skill_ids) {
        if (y > 702) {
            break;
        }
        const auto skill = std::find_if(game.cards().begin(), game.cards().end(),
                                        [&skill_id](const auto& card) { return card.id == skill_id; });
        if (skill == game.cards().end()) {
            continue;
        }
        Panel({556, y, 676, 65}, Color{36, 35, 51, 255});
        type.fit(skill->name, 573, y + 8, 200, 20);
        type.fit("费用 " + std::to_string(skill->cost) + "  ·  " + EffectDescription(*skill), 790, y + 13, 428, 18,
                 MUTED);
        y += 76;
    }
    type.text("定位、称号与技能为 Cardis 演示设计。", 560, 759, 18, MUTED);
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
void DrawCharacterPane(const Typography& type, Portraits& portraits, const cardis::CharacterRoster& roster,
                       cardis::PlayerId& inspected, bool& details, bool& timeline) {
    Panel({28, 86, 238, 574}, Color{37, 29, 46, 255});
    if (Button(type, {40, 98, 98, 38}, "P1", inspected == cardis::PlayerId::FIRST)) {
        inspected = cardis::PlayerId::FIRST;
    }
    if (Button(type, {154, 98, 98, 38}, "P2", inspected == cardis::PlayerId::SECOND)) {
        inspected = cardis::PlayerId::SECOND;
    }
    const auto& character = roster.character(roster.player_character_ids[cardis::Index(inspected)]);
    type.fit(character.name, 44, 151, 210, 27);
    type.fit(character.title, 44, 196, 210, 18, ACCENT_PINK);
    const auto portrait = portraits.get(character.portrait);
    DrawCircleGradient(150, 360, 110, Color{111, 65, 90, 120}, Color{37, 29, 46, 0});
    if (portrait.id != 0) {
        const float scale = std::min(210.0F / portrait.width, 275.0F / portrait.height);
        DrawTexturePro(portrait, {0, 0, static_cast<float>(portrait.width), static_cast<float>(portrait.height)},
                       {147 - portrait.width * scale / 2, 239, portrait.width * scale, portrait.height * scale}, {0, 0},
                       0, WHITE);
    }
    type.fit(roster.group(character.group_id).name, 44, 521, 208, 18, ACCENT_PINK);
    type.fit(GenderLabel(character.gender) + " · " + character.species + " · 吉他", 44, 555, 208, 18, MUTED);
    type.fit(character.combat_role, 44, 589, 208, 18, CYAN);
    if (Button(type, {40, 620, 214, 32}, "角色资料")) {
        details = true;
    }
    Panel({28, 677, 238, 197});
    type.text("操作提示", 44, 690, 20, ACCENT_PINK);
    type.fit("选牌后点目标", 44, 732, 208, 18, MUTED);
    type.fit("选前排后攻击", 44, 768, 208, 18, MUTED);
    type.fit("右键取消选择", 44, 804, 208, 18, MUTED);
    if (Button(type, {40, 838, 214, 32}, "结算与记录")) {
        timeline = true;
    }
}
void DrawPlayer(const Typography& type, const cardis::Game& game, const cardis::CharacterRoster& roster,
                cardis::PlayerId id, float y, const Selection& selection, TargetClick& click, bool interactive) {
    const auto& state = game.state();
    const auto& player = state.players[cardis::Index(id)];
    const auto& character = roster.character(player.character_id);
    const Rectangle bounds{284, y, 848, 81};
    const bool placement =
        selection.hand >= 0 &&
        game.cards()[state.players[cardis::Index(state.priority)].hand[static_cast<std::size_t>(selection.hand)]]
                .kind == cardis::CardKind::CHARACTER;
    const bool legal = !placement && CanTarget(game, selection, {id, 0}).accepted;
    Panel(bounds, id == state.priority ? Color{40, 34, 50, 255} : PANEL);
    if (legal) {
        DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 2, CYAN);
    }
    type.fit("P" + std::to_string(cardis::Index(id) + 1) + "  " + character.name, 300, y + 7, 289, 20);
    type.text(std::to_string(player.life) + " / " + std::to_string(player.max_life), 608, y + 5, 27, ACCENT_PINK);
    type.text("灵力 " + std::to_string(player.mana) + "/" + std::to_string(player.max_mana), 853, y + 10, 20, CYAN);
    type.fit("卡组 " + std::to_string(player.deck.size()) + "  ·  手牌 " + std::to_string(player.hand.size()) +
                 "  ·  墓地 " + std::to_string(player.graveyard.size()),
             300, y + 46, 549, 18, MUTED);
    type.fit("护盾 " + std::to_string(player.shield), 853, y + 46, 255, 18, CREAM);
    if ((selection.hand >= 0 || selection.unit != 0) && !legal) {
        DrawRectangleRec(bounds, Fade(BACKGROUND, 0.20F));
    }
    if (Clicked(bounds, interactive) && !placement && (selection.hand >= 0 || selection.unit != 0)) {
        click = {true, {id, 0}, cardis::Row::FRONT};
    }
}
void DrawRow(const Typography& type, const cardis::Game& game, cardis::PlayerId owner, cardis::Row row, float y,
             Selection& selection, TargetClick& click, bool interactive, std::string& message) {
    std::vector<const cardis::UnitState*> units;
    for (const auto& unit : game.state().players[cardis::Index(owner)].battlefield) {
        if (unit.row == row) {
            units.push_back(&unit);
        }
    }
    for (std::size_t column = 0; column < 3; ++column) {
        const Rectangle bounds{284 + static_cast<float>(column) * 286, y, 276, 84};
        const bool empty = column >= units.size();
        const auto target = cardis::Target{owner, empty ? 0 : units[column]->id};
        const bool placement =
            selection.hand >= 0 &&
            game.cards()[game.state().players[cardis::Index(game.state().priority)].hand[static_cast<std::size_t>(
                             selection.hand)]]
                    .kind == cardis::CardKind::CHARACTER;
        const bool legal = (empty == placement) && CanTarget(game, selection, target, row).accepted;
        Panel(bounds, row == cardis::Row::FRONT ? Color{36, 33, 49, 255} : Color{25, 31, 44, 255});
        const bool selected = !empty && selection.unit == units[column]->id;
        const bool ready = !empty && owner == game.state().priority && CanAttackWith(game, units[column]->id);
        const bool movable =
            !empty && owner == game.state().priority && game.canMove(owner, units[column]->id).accepted;
        const bool focused = selection.hand >= 0 || selection.unit != 0;
        if (legal || selected || (!focused && (ready || movable))) {
            DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, 2, selected ? ACCENT_PINK : CYAN);
        }
        if (empty) {
            type.fit(row == cardis::Row::FRONT ? "前排 · 空位" : "后排 · 空位", bounds.x + 56, y + 24, 208, 18,
                     legal ? CYAN : MUTED);
        } else {
            const auto& unit = *units[column];
            const auto& card = game.cards()[unit.card];
            const std::string keyword = card.guard && card.haste ? "守/疾"
                                        : card.guard             ? "守护"
                                        : card.haste             ? "疾奏"
                                                                 : "";
            type.fit(card.name, bounds.x + 13, y + 6, keyword.empty() ? 250.0F : 155.0F, 18,
                     selected ? ACCENT_PINK : INK);
            if (!keyword.empty()) {
                type.fit(keyword, bounds.x + 177, y + 6, 90, 18, CREAM);
            }
            const std::string condition = unit.exhausted                            ? "横置"
                                          : unit.summoned_turn == game.state().turn ? "登场"
                                                                                    : "就绪";
            type.fit(std::to_string(card.attack) + "攻 / " + std::to_string(card.health - unit.damage) + "血" +
                         (unit.shield > 0 ? " 护" + std::to_string(unit.shield) : "") + " · " + condition,
                     bounds.x + 13, y + 46, 250, 18, ready || legal ? CYAN : MUTED);
        }
        if (focused && !selected && !legal) {
            DrawRectangleRec(bounds, Fade(BACKGROUND, 0.22F));
        }
        if (Clicked(bounds, interactive)) {
            if (selection.hand >= 0 || (selection.unit != 0 && owner != game.state().priority)) {
                if (!empty || placement) {
                    click = {true, target, row};
                }
            } else if (ready || movable) {
                selection.hand = -1;
                selection.unit = units[column]->id;
                message = ready ? "选择敌方目标攻击。" : "点击右侧按钮换排。";
            }
        }
    }
}
void DrawHand(const Typography& type, const cardis::Game& game, Selection& selection, int& page, bool interactive,
              std::string& message) {
    const auto player_id = game.state().priority;
    const auto& player = game.state().players[cardis::Index(player_id)];
    const int pages = std::max(1, static_cast<int>((player.hand.size() + 3) / 4));
    page = std::clamp(page, 0, pages - 1);
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
    for (std::size_t i = start; i < std::min(start + 4, player.hand.size()); ++i) {
        const auto& card = game.cards()[player.hand[i]];
        const Rectangle bounds{284 + static_cast<float>(i - start) * 286, 712, 270, 162};
        const bool selected = selection.hand == static_cast<int>(i);
        const bool available = CanPlayCard(game, i);
        Panel(bounds, selected ? Color{55, 39, 57, 255} : PANEL);
        const bool focused = selection.hand >= 0 || selection.unit != 0;
        if (selected || (available && !focused)) {
            DrawRectangleRoundedLinesEx(bounds, 0.08F, 12, selected ? 2.0F : 1.0F, selected ? ACCENT_PINK : CYAN);
        }
        type.fit(card.name, bounds.x + 13, bounds.y + 9, 202, 20, available ? INK : MUTED);
        DrawCircle(static_cast<int>(bounds.x + 242), static_cast<int>(bounds.y + 23), 20, available ? CYAN : BORDER);
        type.text(std::to_string(card.cost), bounds.x + 234, bounds.y + 6, 21, available ? BACKGROUND : INK);
        if (card.kind == cardis::CardKind::CHARACTER) {
            type.fit("角色 · " + std::to_string(card.attack) + "攻 / " + std::to_string(card.health) + "血",
                     bounds.x + 13, bounds.y + 53, 244, 18, available ? CYAN : MUTED);
            type.fit(std::string(card.guard ? "守护  " : "") + (card.haste ? "疾奏  " : "") + "前后排可放置",
                     bounds.x + 13, bounds.y + 89, 244, 18, MUTED);
        } else {
            type.fit("当前：" + TimedEffects(card, cardis::EffectTiming::ON_RESOLVE), bounds.x + 13, bounds.y + 53, 244,
                     18, available ? INK : MUTED);
            type.fit("回合末：" + TimedEffects(card, cardis::EffectTiming::END_OF_TURN), bounds.x + 13, bounds.y + 89,
                     244, 18, MUTED);
        }
        type.fit(selected    ? "已选择 · 点击目标"
                 : available ? "点击选择"
                             : "暂不可用",
                 bounds.x + 13, bounds.y + 126, 244, 18, selected ? ACCENT_PINK : MUTED);
        if (focused && !selected) {
            DrawRectangleRec(bounds, Fade(BACKGROUND, 0.22F));
        }
        if (!available && interactive && CheckCollisionPointRec(GetMousePosition(), bounds)) {
            message = player.mana < card.cost ? "灵力不足，等待自己的下回合恢复。"
                      : card.kind == cardis::CardKind::CHARACTER
                          ? MessageLabel(game.canCast(player_id, i, cardis::Target{player_id, 0}).error)
                          : "当前没有合法目标。";
        }
        if (Clicked(bounds, interactive && available)) {
            selection.unit = 0;
            selection.hand = selected ? -1 : static_cast<int>(i);
            message = card.kind == cardis::CardKind::CHARACTER ? "点击己方前排或后排的空位。" : EffectDescription(card);
        }
    }
    if (player.hand.empty()) {
        type.text("手牌已用尽。下回合开始时抽牌。", 309, 770, 23, MUTED);
    }
    // Full effect text remains available even when compact card summaries are ellipsized.
    for (std::size_t i = start; interactive && i < std::min(start + 4, player.hand.size()); ++i) {
        const auto& card = game.cards()[player.hand[i]];
        if (card.kind != cardis::CardKind::SKILL ||
            !CheckCollisionPointRec(GetMousePosition(), {284 + static_cast<float>(i - start) * 286, 712, 270, 162})) {
            continue;
        }
        Panel({590, 384, 530, 272}, Color{40, 36, 53, 255});
        type.fit(card.name + " · 完整效果", 608, 400, 496, 21, INK);
        int line = 0;
        for (const auto& effect : cardis::EffectsOf(card)) {
            type.fit(std::string(effect.timing == cardis::EffectTiming::ON_RESOLVE ? "当前结算：" : "回合末：") +
                         StepText(effect),
                     608, 447 + static_cast<float>(line++) * 44, 496, 18, MUTED);
        }
    }
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
void DrawTimeline(const Typography& type, const cardis::Game& game, bool& open, int& tab, int& page) {
    const auto& state = game.state();
    DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BLACK, 0.83F));
    Panel({170, 94, 1100, 712});
    type.text("结算与记录", 205, 114, 28, ACCENT_PINK);
    if (Button(type, {1110, 112, 130, 40}, "关闭")) {
        open = false;
    }
    const std::array<std::string, 3> labels{"当前响应栈", "回合末待触发", "对局记录"};
    for (int i = 0; i < 3; ++i) {
        if (Button(type, {204 + static_cast<float>(i) * 348, 173, 324, 43}, labels[static_cast<std::size_t>(i)],
                   tab == i)) {
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
        const float y = 242 + static_cast<float>(index - begin) * (tab == 2 ? 74.0F : 216.0F);
        if (tab == 2) {
            const auto& event = state.events[count - index - 1];
            type.wrapped("#" + std::to_string(event.sequence) + "  " + MessageLabel(event.message), 208, y, 1020, 2, 18,
                         MUTED);
            continue;
        }
        const auto card_index = tab == 0 ? state.stack[count - index - 1].card : state.scheduled_effects[index].card;
        const auto& card = game.cards()[card_index];
        const auto controller =
            tab == 0 ? state.stack[count - index - 1].controller : state.scheduled_effects[index].controller;
        const auto target = tab == 0 ? state.stack[count - index - 1].target : state.scheduled_effects[index].target;
        const std::string due =
            tab == 1 ? " · 第 " + std::to_string(state.scheduled_effects[index].due_turn) + " 回合末" : "";
        Panel({201, y - 3, 1038, 199}, Color{35, 33, 48, 255});
        type.fit(std::to_string(index + 1) + ". " + card.name + " · P" + std::to_string(cardis::Index(controller) + 1) +
                     " 施放" + due,
                 215, y + 7, 1006, 20, INK);
        type.fit("目标：" + TargetLabel(game, target), 215, y + 48, 1006, 18, CYAN);
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
        type.wrapped(description, 215, y + 93, 1006, 3, 18, MUTED);
    }
    if (count == 0) {
        type.text(tab == 0   ? "当前没有待响应的结算。"
                  : tab == 1 ? "当前没有登记的回合末效果。"
                             : "尚无对局记录。",
                  210, 294, 23, MUTED);
    }
    if (Button(type, {204, 733, 144, 41}, "上一页", false, page > 0)) {
        --page;
    }
    type.fit(std::to_string(page + 1) + " / " + std::to_string(pages) + " 页 · 共 " + std::to_string(count) + " 项",
             466, 740, 575, 18, MUTED);
    if (Button(type, {1096, 733, 144, 41}, "下一页", false, page < pages - 1)) {
        ++page;
    }
}
void DrawSidebar(const Typography& type, const cardis::Game& game) {
    const auto& state = game.state();
    Panel({1150, 299, 262, 178});
    type.fit("当前响应栈 · " + std::to_string(state.stack.size()), 1164, 307, 234, 18, ACCENT_PINK);
    if (state.stack.empty()) {
        type.text("等待出牌或攻击", 1164, 371, 18, MUTED);
    }
    int index = 0;
    for (auto it = state.stack.rbegin(); it != state.stack.rend() && index < 2; ++it, ++index) {
        const auto& card = game.cards()[it->card];
        type.fit(std::to_string(index + 1) + ". " + card.name, 1164, 347 + static_cast<float>(index) * 67, 234, 18,
                 index == 0 ? INK : MUTED);
        const std::string kind = it->kind == cardis::StackKind::ATTACK    ? "攻击"
                                 : it->kind == cardis::StackKind::TRIGGER ? "触发"
                                                                          : "施放";
        type.fit(kind + " → " + TargetLabel(game, it->target), 1164, 379 + static_cast<float>(index) * 67, 234, 18,
                 MUTED);
    }
    Panel({1150, 489, 262, 171});
    type.fit("回合末待触发 · " + std::to_string(state.scheduled_effects.size()), 1164, 499, 234, 18, CREAM);
    if (state.scheduled_effects.empty()) {
        type.text("暂无延迟效果", 1164, 545, 18, MUTED);
        type.wrapped("攻击在当前响应栈结算", 1164, 591, 234, 2, 18, MUTED);
    } else {
        const auto& effect = state.scheduled_effects.front();
        type.fit(game.cards()[effect.card].name, 1164, 540, 234, 18);
        type.fit(TargetLabel(game, effect.target), 1164, 578, 234, 18, MUTED);
        type.fit("T" + std::to_string(effect.due_turn) + "末 · " + StepText(effect.effect), 1164, 616, 234, 18, CREAM);
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
    while (!WindowShouldClose()) {
        const double presentation_time = smoke ? 1.0 : GetTime();
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
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        canvas.begin();
        type.setViewport(canvas.scale(), canvas.origin());
        const auto& state = game.state();
        const bool interactive = !details && !timeline && !handoff && state.phase != cardis::Phase::FINISHED;
        if (details || timeline || handoff) {
            GuiLock();
        }
        if (interactive && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            selection.clear();
        }
        DrawRectangleGradientH(0, 0, WIDTH, 73, Color{39, 28, 44, 255}, BACKGROUND);
        type.text("C A R D I S", 29, 17, 28);
        const std::string actor = "P" + std::to_string(cardis::Index(state.priority) + 1);
        const std::string action = state.phase == cardis::Phase::FINISHED ? "对局结束"
                                   : handoff                              ? "回合交接中"
                                   : selection.hand >= 0                  ? "请选择目标 / 放置位置"
                                   : selection.unit != 0                  ? "请选择攻击目标 / 换排"
                                   : !HasAction(game)                     ? "暂无可用行动 · 请让过"
                                   : !state.stack.empty()                 ? "响应窗口 · 可出牌或让过"
                                                                          : "选择高亮手牌或角色";
        const float pulse = static_cast<float>(std::clamp((phase_until - presentation_time) / 0.8, 0.0, 1.0));
        Panel({284, 7, 848, 68},
              Color{static_cast<unsigned char>(35 + 28 * pulse), static_cast<unsigned char>(31 + 16 * pulse),
                    static_cast<unsigned char>(46 + 18 * pulse), 255});
        type.fit(actor + " · " + action, 302, 9, 811, 22, ACCENT_PINK);
        type.fit("回合 " + std::to_string(state.turn) + " · " + PhaseName(state.phase) + " · 双方确认 " +
                     std::to_string(state.consecutive_passes) + "/2",
                 302, 43, 811, 18, MUTED);
        const bool restart = Button(type, {1197, 18, 215, 39}, "重新开始");
        DrawCharacterPane(type, portraits, roster, inspected, details, timeline);
        if (selection.hand >= 0 || selection.unit != 0) {
            DrawRectangleRec({28, 86, 238, 574}, Fade(BACKGROUND, 0.16F));
        }
        TargetClick target;
        DrawPlayer(type, game, roster, cardis::PlayerId::SECOND, 86, selection, target, interactive);
        DrawRow(type, game, cardis::PlayerId::SECOND, cardis::Row::BACK, 184, selection, target, interactive, message);
        DrawRow(type, game, cardis::PlayerId::SECOND, cardis::Row::FRONT, 279, selection, target, interactive, message);
        DrawLine(297, 374, 1119, 374, BORDER);
        DrawRow(type, game, cardis::PlayerId::FIRST, cardis::Row::FRONT, 386, selection, target, interactive, message);
        DrawRow(type, game, cardis::PlayerId::FIRST, cardis::Row::BACK, 481, selection, target, interactive, message);
        DrawPlayer(type, game, roster, cardis::PlayerId::FIRST, 579, selection, target, interactive);
        Panel({1150, 86, 262, 200});
        type.text(PhaseName(state.phase), 1166, 100, 22, CYAN);
        type.text("P" + std::to_string(cardis::Index(state.priority) + 1) + " 持有优先权", 1166, 141, 18, MUTED);
        const bool pass = Button(type, {1162, 184, 238, 43}, PassLabel(state), true, interactive);
        const bool move =
            Button(type, {1162, 239, 238, 34}, "所选角色换排", false,
                   interactive && selection.unit != 0 && game.canMove(state.priority, selection.unit).accepted);
        DrawSidebar(type, game);
        DrawHand(type, game, selection, page, interactive, message);
        GuiUnlock();
        if (details) {
            DrawCharacterDetails(type, portraits, roster, game, inspected, details);
        }
        if (timeline) {
            DrawTimeline(type, game, timeline, timeline_tab, timeline_page);
        }
        if (handoff) {
            DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BACKGROUND, 0.86F));
            Panel({364, 221, 712, 437}, Color{40, 34, 52, 255});
            const auto next = "P" + std::to_string(cardis::Index(state.active) + 1);
            type.fit("P" + std::to_string(cardis::Index(cardis::Opponent(state.active)) + 1) + " 回合结束", 406, 255,
                     626, 26, MUTED);
            type.fit(next + " 准备行动", 406, 318, 626, 34, ACCENT_PINK);
            type.text(std::to_string(static_cast<int>(std::ceil(handoff_until - presentation_time))), 676, 389, 69,
                      CREAM);
            type.fit("准备好后可跳过，接下来由你行动。", 406, 512, 626, 18, MUTED);
            DrawRectangleRounded({406, 555, static_cast<float>((handoff_until - presentation_time) / 3.0) * 626, 5},
                                 0.4F, 8, ACCENT_PINK);
            if (Button(type, {406, 581, 626, 48}, "准备好了 · 空格跳过", true) || IsKeyPressed(KEY_SPACE)) {
                handoff_until = -1;
            }
        }
        if (state.phase == cardis::Phase::FINISHED && !details && !timeline) {
            DrawRectangle(270, 173, 872, 399, Fade(BLACK, 0.88F));
            type.text(state.winner ? "P" + std::to_string(cardis::Index(*state.winner) + 1) + " 获胜" : "平局", 569,
                      287, 38, ACCENT_PINK);
            type.fit(MessageLabel(state.result), 374, 359, 673, 22, INK);
            type.text("点击右上角重新开始，再奏一曲。", 409, 428, 22, MUTED);
        }
        // Defer commands until drawing is complete, preserving every reference used above.
        if (restart) {
            game.reset();
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
        const auto manifest = argc > 1 ? std::filesystem::path(argv[1])
                                       : std::filesystem::path(GetApplicationDirectory()) / "assets/cards.json";
        return Run(manifest, argc > 2 ? argv[2] : "", argc > 3 ? argv[3] : "");
    } catch (const std::exception& error) {
        std::cerr << "Cardis: " << error.what() << '\n';
        return 1;
    }
}
