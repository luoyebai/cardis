#pragma once
// Geometry for the Cardis client.
//
// Every rectangle the client draws comes from here, so the layout can be
// checked arithmetically (nothing overflows the canvas, HUD groups never
// overlap) without starting a graphics context. Rows are named by screen
// position rather than by rule semantics: the second player sits above the
// rail, the first player below it, matching how the board reads top to bottom.

#include <raylib.h>

namespace cardis::ui {

inline constexpr float kCanvasWidth = 1440.0F;
inline constexpr float kCanvasHeight = 900.0F;

// Board columns: three slots of 276 with 10px gutters inside 848.
inline constexpr float kBoardX = 284.0F;
inline constexpr float kBoardWidth = 848.0F;
inline constexpr float kColumnWidth = 276.0F;
inline constexpr float kColumnGap = 10.0F;
inline constexpr float kColumnStride = kColumnWidth + kColumnGap;

inline constexpr float kRowHeight = 84.0F;
inline constexpr float kPlayerBarHeight = 81.0F;
inline constexpr float kTopPlayerY = 86.0F;
inline constexpr float kTopBackY = 184.0F;
inline constexpr float kTopFrontY = 279.0F;
inline constexpr float kRailY = 364.0F;
inline constexpr float kRailHeight = 21.0F;
inline constexpr float kBottomFrontY = 386.0F;
inline constexpr float kBottomBackY = 481.0F;
inline constexpr float kBottomPlayerY = 579.0F;

inline constexpr float kHeaderHeight = 73.0F;
inline constexpr float kActionX = 284.0F;
inline constexpr float kActionY = 7.0F;
inline constexpr float kActionWidth = 848.0F;
inline constexpr float kActionHeight = 68.0F;
inline constexpr float kRestartX = 1197.0F;
inline constexpr float kRestartY = 18.0F;
inline constexpr float kRestartWidth = 215.0F;
inline constexpr float kRestartHeight = 39.0F;

// Left column: character sheet above, contextual console below.
inline constexpr float kSideX = 28.0F;
inline constexpr float kSideWidth = 238.0F;
inline constexpr float kCharacterY = 86.0F;
inline constexpr float kCharacterHeight = 574.0F;
inline constexpr float kConsoleY = 677.0F;
inline constexpr float kConsoleHeight = 197.0F;

// Right column: phase, stack, scheduled effects.
inline constexpr float kPanelX = 1150.0F;
inline constexpr float kPanelWidth = 262.0F;
inline constexpr float kPhaseY = 86.0F;
inline constexpr float kPhaseHeight = 200.0F;
inline constexpr float kStackY = 299.0F;
inline constexpr float kStackHeight = 178.0F;
inline constexpr float kScheduledY = 489.0F;
inline constexpr float kScheduledHeight = 171.0F;

// Hand: four columns spanning the full width below the board.
inline constexpr float kHandLabelY = 672.0F;
inline constexpr float kHandCardY = 712.0F;
inline constexpr float kHandCardWidth = 270.0F;
inline constexpr float kHandCardHeight = 162.0F;
inline constexpr float kHandColumnStride = 286.0F;
inline constexpr float kHandLift = 8.0F;  // hover raise, still clear of the label row

// Player HUD inside the bar: avatar, name, life, shield, mana.
inline constexpr float kAvatarSize = 36.0F;
inline constexpr float kLifeBarWidth = 280.0F;
inline constexpr float kLifeBarHeight = 18.0F;
inline constexpr float kShieldChipWidth = 62.0F;
inline constexpr int kManaPips = 10;
inline constexpr float kManaPipWidth = 21.0F;
inline constexpr float kManaPipHeight = 12.0F;
inline constexpr float kManaPipGap = 5.0F;
inline constexpr float kPlayerPadding = 16.0F;

[[nodiscard]] inline Rectangle PlayerBar(bool top) {
    return {kBoardX, top ? kTopPlayerY : kBottomPlayerY, kBoardWidth, kPlayerBarHeight};
}
[[nodiscard]] inline Rectangle RowBand(int index) {
    const float ys[4]{kTopBackY, kTopFrontY, kBottomFrontY, kBottomBackY};
    return {kBoardX, ys[index], kBoardWidth, kRowHeight};
}
[[nodiscard]] inline Rectangle Slot(int row_index, int column) {
    return {kBoardX + static_cast<float>(column) * kColumnStride, RowBand(row_index).y, kColumnWidth, kRowHeight};
}
[[nodiscard]] inline Rectangle Rail() { return {kBoardX, kRailY, kBoardWidth, kRailHeight}; }

[[nodiscard]] inline Rectangle Avatar(float bar_y) {
    return {kBoardX + 20.0F, bar_y + 18.0F, kAvatarSize, kAvatarSize};
}
[[nodiscard]] inline Rectangle PlayerName(float bar_y) { return {kBoardX + 70.0F, bar_y + 8.0F, 136.0F, 26.0F}; }
[[nodiscard]] inline Rectangle LifeBar(float bar_y) {
    return {kBoardX + 216.0F, bar_y + 15.0F, kLifeBarWidth, kLifeBarHeight};
}
[[nodiscard]] inline Rectangle ShieldChip(float bar_y) {
    return {kBoardX + 502.0F, bar_y + 15.0F, kShieldChipWidth, kLifeBarHeight};
}
[[nodiscard]] inline Rectangle ManaPipsLabel(float bar_y) { return {kBoardX + 490.0F, bar_y + 52.0F, 358.0F, 22.0F}; }
[[nodiscard]] inline Rectangle PlayerStats(float bar_y) { return {kBoardX + 70.0F, bar_y + 52.0F, 420.0F, 22.0F}; }
[[nodiscard]] inline Rectangle PriorityStripe(float bar_y) { return {kBoardX + 2.0F, bar_y + 8.0F, 4.0F, 65.0F}; }

[[nodiscard]] inline float ManaPipRowWidth() {
    return static_cast<float>(kManaPips) * kManaPipWidth + static_cast<float>(kManaPips - 1) * kManaPipGap;
}
[[nodiscard]] inline Rectangle ManaPip(float bar_y, int index) {
    const float right = kBoardX + kBoardWidth - kPlayerPadding;
    const float left = right - ManaPipRowWidth();
    return {left + static_cast<float>(index) * (kManaPipWidth + kManaPipGap), bar_y + 19.0F, kManaPipWidth,
            kManaPipHeight};
}
[[nodiscard]] inline float ManaPipCentreX() {
    const float right = kBoardX + kBoardWidth - kPlayerPadding;
    return right - ManaPipRowWidth() * 0.5F;
}

[[nodiscard]] inline Rectangle CharacterPane() { return {kSideX, kCharacterY, kSideWidth, kCharacterHeight}; }
[[nodiscard]] inline Rectangle ConsolePane() { return {kSideX, kConsoleY, kSideWidth, kConsoleHeight}; }
[[nodiscard]] inline Rectangle PhasePane() { return {kPanelX, kPhaseY, kPanelWidth, kPhaseHeight}; }
[[nodiscard]] inline Rectangle StackPane() { return {kPanelX, kStackY, kPanelWidth, kStackHeight}; }
[[nodiscard]] inline Rectangle ScheduledPane() { return {kPanelX, kScheduledY, kPanelWidth, kScheduledHeight}; }
[[nodiscard]] inline Rectangle ActionPane() { return {kActionX, kActionY, kActionWidth, kActionHeight}; }
[[nodiscard]] inline Rectangle RestartButton() {
    return {kRestartX, kRestartY, kRestartWidth, kRestartHeight};
}

[[nodiscard]] inline Rectangle HandCard(int column) {
    return {kBoardX + static_cast<float>(column) * kHandColumnStride, kHandCardY, kHandCardWidth, kHandCardHeight};
}
// Hand rows sit above the pagination strip; the lift must not cross it.
[[nodiscard]] inline Rectangle HandCardLifted(int column) {
    Rectangle bounds = HandCard(column);
    bounds.y -= kHandLift;
    return bounds;
}
[[nodiscard]] inline bool InsideCanvas(Rectangle bounds) {
    return bounds.x >= 0.0F && bounds.y >= 0.0F && bounds.x + bounds.width <= kCanvasWidth &&
           bounds.y + bounds.height <= kCanvasHeight;
}
[[nodiscard]] inline bool Contains(Rectangle outer, Rectangle inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}
[[nodiscard]] inline bool Disjoint(Rectangle left, Rectangle right) {
    return left.x + left.width <= right.x || right.x + right.width <= left.x || left.y + left.height <= right.y ||
           right.y + right.height <= left.y;
}

}  // namespace cardis::ui
