// Weapon grip editor.
//
//   weapon_editor [--weapon id] [--screenshot out.png]
//
// Shows one character holding a weapon. Edit the grip position/rotation (the
// model's pose relative to the hand bone) and press Save to write it into
// weapons/<id>.json. Right mouse button orbits, scroll zooms.
#include "mapview/character.h"
#include "mapview/world_mesh.h"

#include "base/paths.h"
#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"
#include "game/map.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace {
using namespace Paper;

template <typename T> T require(std::expected<T, Error> result) {
  if (!result) {
    spdlog::critical("{}", result.error().message);
    std::exit(1);
  }
  if constexpr (!std::is_void_v<T>) {
    return std::move(*result);
  }
}

constexpr float kPi = 3.14159265F;

struct Pose {
  const char *name;
  float speed;
  bool ducked;
};
constexpr Pose kPoses[] = {{"Idle", 0.0F, false},
                           {"Walk", 100.0F, false},
                           {"Jog", 190.0F, false},
                           {"Sprint", 250.0F, false},
                           {"Crouch", 0.0F, true}};

class WeaponEditorApp {
public:
  WeaponEditorApp(Window &window, Renderer &renderer, std::filesystem::path weaponsDir)
      : m_window(window), m_renderer(renderer), m_weaponsDir(std::move(weaponsDir)),
        m_weapons(require(WeaponDb::loadDirectory(m_weaponsDir))) {
    m_shader = require(m_renderer.createShader({"pbr.vert.spv", "pbr.frag.spv"}));
    const ShaderHandle skinned =
        require(m_renderer.createShader({"pbr_skinned.vert.spv", "pbr.frag.spv"}));
    m_scene.ambientLight = glm::vec3(0.3F, 0.31F, 0.35F);
    const auto sun = m_scene.createEntity();
    m_scene.registry().emplace<DirectionalLight>(sun);
    m_scene.registry().get<Transform>(sun).rotation = lookRotation(glm::vec3(-0.4F, -0.6F, -0.7F));

    m_models = std::make_unique<WeaponModels>(m_renderer, m_scene, m_shader, m_weapons);
    m_assets = std::make_unique<CharacterAssets>(m_renderer, skinned);
    if (!m_assets->ok()) {
      spdlog::critical("character model not loaded");
      std::exit(1);
    }
    m_character = std::make_unique<Character>(m_scene, *m_assets, *m_models, m_weapons);

    MapData cube;
    cube.brushes.push_back(MapBrush::box(glm::vec3(-0.5F), glm::vec3(0.5F), "cube"));
    m_cube = require(m_renderer.createMesh(buildMapMeshes(cube).front().mesh));
    m_markerMaterial = makeMaterial({1.0F, 0.2F, 0.2F});
    m_axisMaterial = makeMaterial({0.2F, 0.9F, 0.3F});
    m_marker = spawnCube(m_markerMaterial);
    m_axis = spawnCube(m_axisMaterial);

    m_camera.nearPlane = 1.0F;
    m_camera.farPlane = 5000.0F;
    m_renderer.setUiCallback([this] { drawUi(); });
  }

  ~WeaponEditorApp() {
    m_renderer.setUiCallback(nullptr);
    m_character.reset();
    m_assets.reset();
    m_models.reset();
    m_renderer.destroyMaterial(m_markerMaterial);
    m_renderer.destroyMaterial(m_axisMaterial);
    m_renderer.destroyMesh(m_cube);
  }

  void selectWeapon(const std::string &id) {
    const int index = m_weapons.indexOf(id);
    if (index < 0) {
      spdlog::warn("unknown weapon '{}'", id);
      return;
    }
    m_selected = static_cast<std::size_t>(index);
  }
  void screenshotAndQuit(const char *path) { m_screenshot = path; }

  void run() {
    double last = m_window.time();
    int frame = 0;
    while (!m_window.shouldClose() && !m_quit) {
      m_window.update();
      ++frame;
      if (m_screenshot != nullptr) {
        if (frame == 30) m_renderer.requestScreenshot(m_screenshot);
        if (frame == 31) m_quit = true;
      }
      const double now = m_window.time();
      const auto dt = static_cast<float>(now - last);
      last = now;
      updateCamera();
      updateCharacter(dt);
      TransformSystem::update(m_scene);
      require(m_renderer.render(m_scene, cameraNow()));
    }
  }

private:
  MaterialHandle makeMaterial(const glm::vec3 &color) {
    const auto material = require(m_renderer.createMaterial(m_shader, {}));
    require(m_renderer.setMaterialParam(material, "baseColorFactor", glm::vec4(color, 1.0F)));
    require(m_renderer.setMaterialParam(material, "metallicFactor", 0.0F));
    require(m_renderer.setMaterialParam(material, "roughnessFactor", 0.9F));
    require(m_renderer.setMaterialParam(material, "emissiveFactor", color));
    require(m_renderer.setMaterialParam(material, "normalScale", 1.0F));
    require(m_renderer.setMaterialParam(material, "occlusionStrength", 1.0F));
    require(m_renderer.setMaterialParam(material, "alphaCutoff", 0.5F));
    return material;
  }

  entt::entity spawnCube(MaterialHandle material) {
    const entt::entity e = m_scene.createEntity();
    m_scene.registry().emplace<MeshRenderer>(e, MeshRenderer{m_cube, material});
    m_scene.registry().get<Transform>(e).scale = glm::vec3(0.0F);
    return e;
  }

  // The camera orbits the character's chest.
  void updateCamera() {
    if (m_renderer.uiWantsMouse()) {
      return;
    }
    if (m_window.mouseDown(MouseButton::Right)) {
      const glm::dvec2 d = m_window.mouseDelta();
      m_orbitYaw -= static_cast<float>(d.x) * 0.006F;
      m_orbitPitch = std::clamp(m_orbitPitch - static_cast<float>(d.y) * 0.006F, -1.5F, 1.5F);
    }
    m_distance = std::clamp(m_distance * std::pow(0.9F, static_cast<float>(m_window.scrollDelta().y)),
                            15.0F, 600.0F);
  }

  [[nodiscard]] Camera cameraNow() const {
    const glm::quat rotation = glm::angleAxis(m_orbitYaw, glm::vec3(0, 1, 0)) *
                               glm::angleAxis(m_orbitPitch, glm::vec3(1, 0, 0));
    Camera c = m_camera;
    c.rotation = rotation;
    c.position = kTarget - rotation * glm::vec3(0, 0, -1) * m_distance;
    return c;
  }

  void updateCharacter(float dt) {
    const Pose &pose = kPoses[m_pose];
    NetPlayer player;
    player.state.origin = glm::vec3(0.0F);
    player.state.onGround = true;
    player.state.ducked = pose.ducked;
    player.state.velocity = glm::vec3(0.0F, 0.0F, -pose.speed); // yaw 0 looks along -Z
    player.yaw = 0.0F;
    player.pitch = m_aimPitch;
    player.weapon = static_cast<std::uint8_t>(m_selected);
    m_character->update(dt, player);
    // Only the selected weapon is shown (the character hides the others).

    // Muzzle marker and barrel line, from the held model's transform.
    const WeaponDef &def = m_weapons.at(m_selected);
    const entt::entity gun = m_character->gun(m_selected);
    auto &marker = m_scene.registry().get<Transform>(m_marker);
    auto &axis = m_scene.registry().get<Transform>(m_axis);
    if (gun == entt::null || !m_showMarkers) {
      marker.scale = axis.scale = glm::vec3(0.0F);
      return;
    }
    const glm::mat4 m = m_scene.registry().get<Transform>(gun).matrix();
    const glm::vec3 muzzle = glm::vec3(m * glm::vec4(def.muzzle, 1.0F));
    const glm::vec3 forward = glm::normalize(glm::vec3(m * glm::vec4(1, 0, 0, 0)));
    marker.position = muzzle;
    marker.scale = glm::vec3(1.2F);
    axis.position = muzzle + forward * 30.0F;
    axis.rotation = lookRotation(forward);
    axis.scale = glm::vec3(0.4F, 0.4F, 60.0F);
  }

  void saveCurrent() {
    const WeaponDef &def = m_weapons.at(m_selected);
    const auto file = m_weaponsDir / (def.id + ".json");
    const auto result = saveWeaponGrip(file, def.gripPosition, def.gripRotation);
    m_status = result ? "Saved " + file.string() : "Save failed: " + result.error().message;
    if (result) {
      m_saved[m_selected] = {def.gripPosition, def.gripRotation};
    }
  }

  void drawUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Weapon grip");
    if (ImGui::BeginCombo("Weapon", m_weapons.at(m_selected).id.c_str())) {
      for (std::size_t i = 0; i < m_weapons.size(); ++i) {
        if (ImGui::Selectable(m_weapons.at(i).id.c_str(), i == m_selected)) {
          m_selected = i;
        }
      }
      ImGui::EndCombo();
    }
    WeaponDef &def = m_weapons.edit(m_selected);
    ImGui::TextDisabled("model: %s", def.model.empty() ? "(none)" : def.model.c_str());
    m_saved.try_emplace(m_selected, def.gripPosition, def.gripRotation);

    ImGui::SeparatorText("Grip (hand bone space)");
    ImGui::DragFloat3("Position (m)", &def.gripPosition.x, 0.002F, -1.0F, 1.0F, "%.3f");
    ImGui::DragFloat3("Rotation (deg)", &def.gripRotation.x, 0.5F, -360.0F, 360.0F, "%.1f");
    if (ImGui::Button("X +90")) def.gripRotation.x += 90.0F;
    ImGui::SameLine();
    if (ImGui::Button("Y +90")) def.gripRotation.y += 90.0F;
    ImGui::SameLine();
    if (ImGui::Button("Z +90")) def.gripRotation.z += 90.0F;
    if (ImGui::Button("Revert")) {
      def.gripPosition = m_saved[m_selected].first;
      def.gripRotation = m_saved[m_selected].second;
    }
    ImGui::SameLine();
    if (ImGui::Button("Zero")) {
      def.gripPosition = glm::vec3(0.0F);
      def.gripRotation = glm::vec3(0.0F);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) saveCurrent();

    ImGui::SeparatorText("Preview");
    if (ImGui::BeginCombo("Pose", kPoses[m_pose].name)) {
      for (int i = 0; i < static_cast<int>(std::size(kPoses)); ++i) {
        if (ImGui::Selectable(kPoses[i].name, i == m_pose)) m_pose = i;
      }
      ImGui::EndCombo();
    }
    ImGui::SliderFloat("Aim pitch", &m_aimPitch, -1.0F, 1.0F);
    ImGui::Checkbox("Muzzle marker and barrel line", &m_showMarkers);
    if (ImGui::Button("Side")) { m_orbitYaw = 0.5F * kPi; m_orbitPitch = 0.0F; }
    ImGui::SameLine();
    if (ImGui::Button("Front")) { m_orbitYaw = 0.0F; m_orbitPitch = 0.0F; }
    ImGui::SameLine();
    if (ImGui::Button("Back")) { m_orbitYaw = kPi; m_orbitPitch = 0.0F; }
    ImGui::SameLine();
    if (ImGui::Button("Top")) { m_orbitYaw = 0.5F * kPi; m_orbitPitch = -1.5F; }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End();
  }

  static constexpr glm::vec3 kTarget{0.0F, 45.0F, 0.0F};

  Window &m_window;
  Renderer &m_renderer;
  std::filesystem::path m_weaponsDir;
  WeaponDb m_weapons;
  Scene m_scene;
  Camera m_camera;
  ShaderHandle m_shader;
  std::unique_ptr<WeaponModels> m_models;
  std::unique_ptr<CharacterAssets> m_assets;
  std::unique_ptr<Character> m_character;
  MeshHandle m_cube;
  MaterialHandle m_markerMaterial, m_axisMaterial;
  entt::entity m_marker = entt::null, m_axis = entt::null;

  std::size_t m_selected = 0;
  int m_pose = 0;
  float m_aimPitch = 0.0F;
  bool m_showMarkers = true;
  float m_orbitYaw = 0.5F * kPi;
  float m_orbitPitch = 0.0F;
  float m_distance = 130.0F;
  std::map<std::size_t, std::pair<glm::vec3, glm::vec3>> m_saved;
  std::string m_status = "Right mouse button orbits, scroll zooms.";
  bool m_quit = false;
  const char *m_screenshot = nullptr;
};
} // namespace

int main(int argc, char **argv) {
  auto window = require(Window::create({.width = 1280, .height = 800, .title = "Paper Weapon Editor"}));
  auto renderer = require(Renderer::create(*window));
  WeaponEditorApp app(*window, *renderer, Paper::dataDir("weapons", PAPER_WEAPONS_DIR));
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      app.screenshotAndQuit(argv[++i]);
    } else if (arg == "--weapon" && i + 1 < argc) {
      app.selectWeapon(argv[++i]);
    }
  }
  app.run();
  return 0;
}
