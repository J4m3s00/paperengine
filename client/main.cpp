#include "local_player.h"
#include "world_mesh.h"

#include "base/cvar.h"
#include "base/profiler.h"
#include "core/debug/debug_overlay.h"
#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"
#include "game/map.h"
#include "game/movement.h"
#include "game/replay.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace {
// Logs the error and exits; the client treats any engine error as fatal.
template <typename T> T require(std::expected<T, Paper::Error> result) {
  if (!result) {
    spdlog::critical("{}", result.error().message);
    std::exit(1);
  }
  if constexpr (!std::is_void_v<T>) {
    return std::move(*result);
  }
}

// Light grid on white so world-space scale is readable while moving.
Paper::Image makeGrid(std::uint32_t size, std::uint32_t cells) {
  Paper::Image image{size, size, std::vector<std::uint8_t>(size * size * 4)};
  const std::uint32_t cell = size / cells;
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const bool line = x % cell < 2 || y % cell < 2;
      const std::uint8_t v = line ? 150 : 235;
      std::uint8_t *px = &image.pixels[(y * size + x) * 4];
      px[0] = px[1] = px[2] = v;
      px[3] = 255;
    }
  }
  return image;
}

glm::vec3 colorFor(const std::string &material) {
  static const std::map<std::string, glm::vec3> palette{
      {"floor", {0.45F, 0.47F, 0.5F}},   {"wall", {0.6F, 0.55F, 0.5F}},
      {"stairs", {0.7F, 0.6F, 0.4F}},    {"platform", {0.6F, 0.35F, 0.35F}},
      {"ramp", {0.3F, 0.5F, 0.75F}},     {"surf", {0.2F, 0.75F, 0.55F}},
      {"box", {0.85F, 0.65F, 0.2F}},     {"pit", {0.2F, 0.2F, 0.25F}},
      {"ceiling", {0.5F, 0.4F, 0.6F}},   {"pillar", {0.8F, 0.4F, 0.2F}}};
  const auto it = palette.find(material);
  return it != palette.end() ? it->second : glm::vec3(0.6F);
}
} // namespace

// Usage: client [--map map.json] [--screenshot out.png] [--record out.replay.json]
int main(int argc, char **argv) {
  std::filesystem::path mapPath = std::filesystem::path(PAPER_MAPS_DIR) / "dev_world.json";
  const char *screenshotPath = nullptr;
  const char *recordPath = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--map" && i + 1 < argc) {
      mapPath = argv[++i];
    } else if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (arg == "--record" && i + 1 < argc) {
      recordPath = argv[++i];
    }
  }

  const Paper::MapData map = require(Paper::loadMap(mapPath));
  const Paper::CollisionWorld collision = require(map.buildCollision());
  spdlog::info("Loaded {} ({} brushes, {} entities)", mapPath.string(), map.brushes.size(),
               map.entities.size());

  auto window = require(Paper::Window::create({.width = 1280, .height = 720, .title = "Paper"}));
  auto renderer = require(Paper::Renderer::create(*window));
  const auto shader = require(renderer->createShader({"pbr.vert.spv", "pbr.frag.spv"}));
  const auto grid = require(renderer->createTexture(makeGrid(256, 4), Paper::ColorSpace::Srgb));

  Paper::Scene scene;
  scene.ambientLight = glm::vec3(0.12F, 0.13F, 0.15F);
  const auto sun = scene.createEntity();
  scene.registry().emplace<Paper::DirectionalLight>(sun);
  scene.registry().get<Paper::Transform>(sun).rotation =
      Paper::lookRotation(glm::vec3(-0.4F, -0.8F, -0.5F));

  std::vector<Paper::MeshHandle> meshHandles;
  std::vector<Paper::MaterialHandle> materialHandles;
  for (const MapMesh &part : buildMapMeshes(map)) {
    const auto mesh = require(renderer->createMesh(part.mesh));
    const auto material = require(renderer->createMaterial(shader, {}));
    const glm::vec3 color = colorFor(part.material);
    require(renderer->setMaterialParam(material, "baseColorFactor", glm::vec4(color, 1.0F)));
    require(renderer->setMaterialParam(material, "metallicFactor", 0.0F));
    require(renderer->setMaterialParam(material, "roughnessFactor", 0.9F));
    require(renderer->setMaterialParam(material, "emissiveFactor", glm::vec3(0.0F)));
    require(renderer->setMaterialParam(material, "normalScale", 1.0F));
    require(renderer->setMaterialParam(material, "occlusionStrength", 1.0F));
    require(renderer->setMaterialParam(material, "alphaCutoff", 0.5F));
    require(renderer->setMaterialTexture(material, "baseColorTexture", grid));
    const auto entity = scene.createEntity();
    scene.registry().emplace<Paper::MeshRenderer>(entity, mesh, material);
    meshHandles.push_back(mesh);
    materialHandles.push_back(material);
  }

  Paper::MovementCvars movementCvars;
  Paper::DebugOverlay overlay;
  overlay.setVisible(screenshotPath != nullptr);
  float lastFrameSeconds = 0.0F;
  renderer->setUiCallback([&] { overlay.draw(lastFrameSeconds); });
  if (screenshotPath == nullptr) {
    window->setCursorMode(Paper::CursorMode::Captured);
  }

  LocalPlayer player(collision, movementCvars);
  glm::vec3 spawnOrigin(0.0F, 2.0F, 0.0F);
  float spawnYaw = 0.0F;
  if (const auto spawns = map.entitiesOfType("spawn"); !spawns.empty()) {
    spawnOrigin = spawns.front().origin;
    spawnYaw = spawns.front().yaw;
  }
  if (recordPath != nullptr) {
    player.enableRecording(mapPath.filename().string());
  }
  player.respawn(spawnOrigin, spawnYaw);

  Paper::Camera camera;
  camera.nearPlane = 1.0F;
  camera.farPlane = 8000.0F;
  camera.fovY = glm::radians(90.0F);

  bool f1Held = false;
  bool rHeld = false;
  int frame = 0;
  double lastTime = window->time();
  while (!window->shouldClose()) {
    window->update();
    ++frame;
    if (screenshotPath != nullptr) {
      if (frame == 30) {
        renderer->requestScreenshot(screenshotPath);
      } else if (frame == 31) {
        window->requestClose();
      }
    }
    if (window->keyDown(Paper::Key::Escape)) {
      window->requestClose();
    }

    // F1 toggles the debug overlay; with it open the cursor is free.
    const bool f1 = window->keyDown(Paper::Key::F1);
    if (f1 && !f1Held) {
      overlay.toggle();
      window->setCursorMode(overlay.visible() ? Paper::CursorMode::Normal
                                              : Paper::CursorMode::Captured);
    }
    f1Held = f1;
    const bool r = window->keyDown(Paper::Key::R);
    if (r && !rHeld) {
      player.respawn(spawnOrigin, spawnYaw);
    }
    rHeld = r;

    const double now = window->time();
    lastFrameSeconds = static_cast<float>(now - lastTime);
    lastTime = now;

    {
      PAPER_ZONE("player update");
      player.update(*window, lastFrameSeconds, !overlay.visible());
    }
    Paper::TransformSystem::update(scene);
    require(renderer->render(scene, player.camera(camera)));
    PAPER_FRAME_MARK();
  }

  if (recordPath != nullptr) {
    if (const auto recording = player.recording()) {
      require(Paper::saveReplay(recordPath, *recording));
      spdlog::info("Recorded {} ticks to {}", recording->cmds.size(), recordPath);
    }
  }

  for (const auto material : materialHandles) {
    renderer->destroyMaterial(material);
  }
  for (const auto mesh : meshHandles) {
    renderer->destroyMesh(mesh);
  }
  return 0;
}
