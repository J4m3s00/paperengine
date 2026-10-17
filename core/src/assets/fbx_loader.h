#pragma once

#include "base/error.h"
#include "core/assets/model.h"
#include "core/renderer/renderer.h"

#include <expected>
#include <filesystem>

namespace Paper {
// Imports an FBX file (static meshes, node hierarchy, materials and the
// textures they name) into a Model, in meters with Y up. Like importGltf, it
// only uses the Renderer's public resource API and cleans up on failure.
[[nodiscard]] std::expected<Model, Error>
importFbx(Renderer &renderer, const std::filesystem::path &path, ShaderHandle shader);
} // namespace Paper
