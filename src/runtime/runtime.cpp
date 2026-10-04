#include <memory>
#include <utility>

#include <cordis/core/registry.hpp>

#include "cardis/content/catalog.hpp"
#include "cardis/runtime/runtime.hpp"

namespace cardis {
std::shared_ptr<cordis::Context> CreateRuntime(const std::filesystem::path& manifest) {
    auto context = cordis::Context::Create();
    // Parse before plugin installation: startup errors must reach the application boundary.
    auto cards = LoadCatalog(manifest);
    auto roster = std::make_shared<CharacterRoster>(LoadRoster(manifest.parent_path() / "characters.json", cards));
    const auto& first = roster->character(roster->player_character_ids[0]);
    const auto& second = roster->character(roster->player_character_ids[1]);
    auto game = std::make_shared<Game>(std::move(cards), std::array<CharacterDefinition, 2>{first, second});
    context->plugin(cordis::MakePlugin("cardis.match",
                                       [game = std::move(game), roster = std::move(roster)](cordis::Context& scope) {
                                           scope.provide<CharacterRoster>(roster);
                                           scope.provide<Game>(game);
                                       }));
    static_cast<void>(context->require<Game>());
    return context;
}
}  // namespace cardis
