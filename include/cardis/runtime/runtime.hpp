#pragma once

#include <filesystem>
#include <memory>

#include <cordis/core/context.hpp>

namespace cardis {
[[nodiscard]] std::shared_ptr<cordis::Context> CreateRuntime(const std::filesystem::path& manifest);
}  // namespace cardis
