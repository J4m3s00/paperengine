#pragma once

#include "mapview/asset_paths.h"

#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "game/weapon_def.h"

#include <spdlog/spdlog.h>

#include <map>
#include <string>

// The gun models (Styloo pack) used for the view model and for the weapons
// characters carry. Loaded once per file; every use gets its own entity tree.
class WeaponModels {
public:
  WeaponModels(Paper::Renderer &renderer, Paper::Scene &scene, Paper::ShaderHandle shader,
               const Paper::WeaponDb &weapons)
      : m_renderer(renderer), m_scene(scene), m_shader(shader), m_weapons(weapons) {
    for (const Paper::WeaponDef &w : weapons.all()) {
      load(w.model);
    }
  }
  ~WeaponModels() {
    for (const auto &[stem, handle] : m_models) {
      if (handle) {
        m_renderer.destroyModel(*handle);
      }
    }
  }
  WeaponModels(const WeaponModels &) = delete;
  WeaponModels &operator=(const WeaponModels &) = delete;

  // A new, hidden copy of weapon `index`'s model; null if it has no model.
  // The caller moves it by setting the root entity's Transform.
  [[nodiscard]] entt::entity spawn(std::size_t index) {
    const Paper::WeaponDef &def = m_weapons.at(index);
    const auto it = m_models.find(def.model);
    if (def.model.empty() || it == m_models.end() || !it->second) {
      return entt::null;
    }
    const entt::entity root = m_scene.instantiate(m_renderer.model(*it->second));
    m_scene.registry().get<Paper::Transform>(root).scale = glm::vec3(0.0F);
    return root;
  }

private:
  void load(const std::string &stem) {
    if (stem.empty() || m_models.contains(stem)) {
      return;
    }
    auto model = m_renderer.loadModel(Paper::gunModelPath(stem), m_shader);
    if (!model) {
      spdlog::warn("gun model '{}' not loaded: {}", stem, model.error().message);
      m_models[stem] = std::nullopt;
      return;
    }
    // The pack asks for matte, non-metallic shading (its textures carry the color).
    for (const Paper::MaterialHandle material : m_renderer.model(*model).materials) {
      (void)m_renderer.setMaterialParam(material, "metallicFactor", 0.0F);
      (void)m_renderer.setMaterialParam(material, "roughnessFactor", 0.85F);
    }
    m_models[stem] = *model;
  }

  Paper::Renderer &m_renderer;
  Paper::Scene &m_scene;
  Paper::ShaderHandle m_shader;
  const Paper::WeaponDb &m_weapons;
  std::map<std::string, std::optional<Paper::ModelHandle>> m_models;
};
