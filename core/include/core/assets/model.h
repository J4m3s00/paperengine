#pragma once

#include "core/handle.h"
#include "core/renderer/material.h"
#include "core/renderer/mesh.h"
#include "core/renderer/texture.h"
#include "core/scene/components.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Paper {
struct ModelNode {
  std::string name;
  Transform transform;
  std::vector<std::uint32_t> children; // Indices into Model::nodes.
  // One entry per glTF primitive of the node's mesh; Scene::instantiate
  // creates a child entity with a MeshRenderer for each.
  std::vector<MeshRenderer> primitives;
};

// Result of Renderer::loadModel: a node tree plus the GPU resources it refers
// to. The Renderer owns the resources and frees them in destroyModel; the
// shader passed to loadModel stays with the caller.
struct Model {
  std::vector<ModelNode> nodes;
  std::vector<std::uint32_t> rootNodes;
  // Axis-aligned bounds of all meshes in model space (valid if any mesh).
  glm::vec3 boundsMin{0.0F};
  glm::vec3 boundsMax{0.0F};

  std::vector<MeshHandle> meshes;
  std::vector<MaterialHandle> materials;
  std::vector<TextureHandle> textures;
};

struct ModelTag;
using ModelHandle = Handle<ModelTag>;
} // namespace Paper
