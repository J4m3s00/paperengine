#pragma once

#include "core/assets/model.h"
#include "core/error.h"
#include "core/renderer/renderer.h"

#include <expected>
#include <filesystem>

namespace Paper {
// Imports a glTF/GLB file through the Renderer's public resource API. On
// failure everything created so far is destroyed again.
[[nodiscard]] std::expected<Model, Error>
importGltf(Renderer &renderer, const std::filesystem::path &path,
           ShaderHandle shader);

// Destroys the materials, textures and meshes owned by `model`.
void destroyModelResources(Renderer &renderer, const Model &model);
} // namespace Paper
