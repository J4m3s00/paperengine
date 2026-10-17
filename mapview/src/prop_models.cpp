#include "mapview/prop_models.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>

namespace {
constexpr float kUnitsPerMeter = 39.3701F;
}

PropModels::PropModels(Paper::Renderer &renderer, Paper::Scene &scene, Paper::ShaderHandle shader)
    : m_renderer(renderer), m_scene(scene), m_shader(shader) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(Paper::propPiecesDir(), ec)) {
    if (entry.path().extension() == ".fbx") {
      m_catalog.push_back(entry.path().stem().string());
    }
  }
  std::sort(m_catalog.begin(), m_catalog.end());
}

PropModels::~PropModels() {
  for (const auto &[name, handle] : m_models) {
    if (handle) {
      m_renderer.destroyModel(*handle);
    }
  }
}

const Paper::Model *PropModels::load(const std::string &name) {
  const auto it = m_models.find(name);
  if (it != m_models.end()) {
    return it->second ? &m_renderer.model(*it->second) : nullptr;
  }
  const auto known = std::find(m_catalog.begin(), m_catalog.end(), name);
  if (known == m_catalog.end()) {
    m_models[name] = std::nullopt;
    return nullptr;
  }
  auto model = m_renderer.loadModel(Paper::propPiecesDir() / (name + ".fbx"), m_shader);
  if (!model) {
    spdlog::warn("prop '{}' not loaded: {}", name, model.error().message);
    m_models[name] = std::nullopt;
    return nullptr;
  }
  m_models[name] = *model;
  return &m_renderer.model(*model);
}

std::optional<std::pair<glm::vec3, glm::vec3>> PropModels::extents(const std::string &name) {
  const Paper::Model *model = load(name);
  if (model == nullptr) {
    return std::nullopt;
  }
  return std::pair{model->boundsMin, model->boundsMax};
}

entt::entity PropModels::spawn(const Paper::MapProp &prop) {
  const Paper::Model *model = load(prop.model);
  if (model == nullptr) {
    return entt::null;
  }
  const entt::entity root = m_scene.createEntity();
  auto &rt = m_scene.registry().get<Paper::Transform>(root);
  rt.position = prop.origin;
  rt.rotation = glm::angleAxis(prop.yaw, glm::vec3(0.0F, 1.0F, 0.0F));
  rt.scale = glm::vec3(kUnitsPerMeter * prop.scale);
  // The pieces sit wherever they were placed in the authoring scene: pull the
  // middle of the base to the prop's origin.
  const entt::entity body = m_scene.instantiate(*model);
  const glm::vec3 base((model->boundsMin.x + model->boundsMax.x) * 0.5F, model->boundsMin.y,
                       (model->boundsMin.z + model->boundsMax.z) * 0.5F);
  m_scene.registry().get<Paper::Transform>(body).position = -base;
  m_scene.setParent(body, root);
  return root;
}
