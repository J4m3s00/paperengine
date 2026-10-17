#pragma once

#include "asset_paths.h"

#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "game/map.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The environment kit (FBX pieces): loads models by name on demand and puts
// copies of them into a scene. Shared by the game client and the editor.
class PropModels {
public:
  PropModels(Paper::Renderer &renderer, Paper::Scene &scene, Paper::ShaderHandle shader);
  ~PropModels();
  PropModels(const PropModels &) = delete;
  PropModels &operator=(const PropModels &) = delete;

  // Names of the pieces in the kit (file stems), sorted.
  [[nodiscard]] const std::vector<std::string> &catalog() const { return m_catalog; }
  // Bounds of a piece in its model space, in meters (nullopt: not in the kit).
  [[nodiscard]] std::optional<std::pair<glm::vec3, glm::vec3>> extents(const std::string &name);
  // A new entity tree for `prop`: meters scaled to units, turned by its yaw,
  // the middle of the base at its origin. Null if the piece is unknown.
  // Destroy with Scene::destroyTree.
  [[nodiscard]] entt::entity spawn(const Paper::MapProp &prop);

private:
  const Paper::Model *load(const std::string &name);

  Paper::Renderer &m_renderer;
  Paper::Scene &m_scene;
  Paper::ShaderHandle m_shader;
  std::vector<std::string> m_catalog;
  std::map<std::string, std::optional<Paper::ModelHandle>> m_models;
};
