#pragma once

#include <filesystem>
#include <vector>

#include "cardis/core/game.hpp"

namespace cardis {
// Paths are relative to the manifest directory; missing portraits use a UI placeholder.
[[nodiscard]] std::vector<CardDefinition> LoadCatalog(const std::filesystem::path& manifest);
[[nodiscard]] CharacterRoster LoadRoster(const std::filesystem::path& manifest,
                                         const std::vector<CardDefinition>& cards);
}  // namespace cardis
