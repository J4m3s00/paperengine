#include "local_player.h"
#include "combat_view.h"
#include "movement_hud.h"
#include "net_hud.h"
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
#include "game/weapon_def.h"
#include "net/conditioned_transport.h"
#include "net/enet_transport.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
//               [--set cvar value]... [--connect host[:port]] [--name N]
//               [--lag RTTms] [--jitter ms] [--loss 0..1]
int main(int argc, char **argv) {
  std::filesystem::path mapPath = std::filesystem::path(PAPER_MAPS_DIR) / "dev_world.json";
  const char *screenshotPath = nullptr;
  const char *recordPath = nullptr;
  std::vector<std::pair<std::string, std::string>> cvarOverrides;
  std::string connectTo;
  std::string playerName = "player";
  Paper::NetworkConditions simulated; // one-way, applied to both directions
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--map" && i + 1 < argc) {
      mapPath = argv[++i];
    } else if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (arg == "--record" && i + 1 < argc) {
      recordPath = argv[++i];
    } else if (arg == "--connect" && i + 1 < argc) {
      connectTo = argv[++i];
    } else if (arg == "--name" && i + 1 < argc) {
      playerName = argv[++i];
    } else if (arg == "--lag" && i + 1 < argc) {
      simulated.latencyMs = std::atof(argv[++i]) / 2.0;
    } else if (arg == "--jitter" && i + 1 < argc) {
      simulated.jitterMs = std::atof(argv[++i]);
    } else if (arg == "--loss" && i + 1 < argc) {
      simulated.loss = std::atof(argv[++i]);
    } else if (arg == "--set" && i + 2 < argc) {
      cvarOverrides.emplace_back(argv[i + 1], argv[i + 2]);
      i += 2;
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

  const Paper::WeaponDb weapons = require(Paper::WeaponDb::loadDirectory(PAPER_WEAPONS_DIR));
  Paper::MovementCvars movementCvars;
  Paper::DebugOverlay overlay;
  overlay.setVisible(screenshotPath != nullptr);
  float lastFrameSeconds = 0.0F;
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

  // Online: connect, predict through the session, draw the other players.
  std::unique_ptr<Paper::EnetTransport> enet;
  std::unique_ptr<Paper::ConditionedTransport> conditioned;
  std::unique_ptr<Paper::ClientSession> session;
  std::vector<entt::entity> remoteEntities;
  NetHud netHud;
  std::unique_ptr<CombatView> combatView;
  const auto netStart = std::chrono::steady_clock::now();
  if (!connectTo.empty()) {
    const auto colon = connectTo.find(':');
    const std::string host = connectTo.substr(0, colon);
    const auto port = colon == std::string::npos
                          ? Paper::kDefaultServerPort
                          : static_cast<std::uint16_t>(std::atoi(connectTo.c_str() + colon + 1));
    enet = require(Paper::EnetTransport::createClient());
    const Paper::PeerId peer = require(enet->connect(host, port));
    Paper::Transport *transport = enet.get();
    if (simulated.latencyMs > 0 || simulated.jitterMs > 0 || simulated.loss > 0) {
      conditioned = std::make_unique<Paper::ConditionedTransport>(*enet, simulated);
      transport = conditioned.get();
      spdlog::info("simulating {:.0f} ms RTT, {:.0f} ms jitter, {:.1f}% loss",
                   simulated.latencyMs * 2, simulated.jitterMs, simulated.loss * 100.0);
    }
    session = std::make_unique<Paper::ClientSession>(*transport, peer, collision, weapons,
                                                     movementCvars.params(), playerName);
    player.setSession(session.get());
    spdlog::info("connecting to {}:{}", host, port);

    Paper::BrushDesc hullBrush = Paper::boxBrush(Paper::kStandingHull.mins, Paper::kStandingHull.maxs);
    Paper::MapData hullMap;
    hullMap.brushes.push_back({hullBrush, "player"});
    const auto hullMesh = require(renderer->createMesh(buildMapMeshes(hullMap).front().mesh));
    const auto makeMaterial = [&](const glm::vec3 &color, const glm::vec3 &emissive) {
      const auto material = require(renderer->createMaterial(shader, {}));
      require(renderer->setMaterialParam(material, "baseColorFactor", glm::vec4(color, 1.0F)));
      require(renderer->setMaterialParam(material, "metallicFactor", 0.0F));
      require(renderer->setMaterialParam(material, "roughnessFactor", 0.8F));
      require(renderer->setMaterialParam(material, "emissiveFactor", emissive));
      require(renderer->setMaterialParam(material, "normalScale", 1.0F));
      require(renderer->setMaterialParam(material, "occlusionStrength", 1.0F));
      require(renderer->setMaterialParam(material, "alphaCutoff", 0.5F));
      materialHandles.push_back(material);
      return material;
    };
    const auto hullMaterial = makeMaterial({0.9F, 0.2F, 0.2F}, glm::vec3(0.0F));
    Paper::MapData cubeMap;
    cubeMap.brushes.push_back({Paper::boxBrush(glm::vec3(-0.5F), glm::vec3(0.5F)), "cube"});
    const auto cubeMesh = require(renderer->createMesh(buildMapMeshes(cubeMap).front().mesh));
    meshHandles.push_back(cubeMesh);
    combatView = std::make_unique<CombatView>(
        scene, CombatView::Assets{cubeMesh, makeMaterial({0.15F, 0.15F, 0.17F}, glm::vec3(0.0F)),
                                  makeMaterial({0.1F, 0.1F, 0.1F}, {2.5F, 2.0F, 0.8F}),
                                  makeMaterial({0.1F, 0.1F, 0.1F}, {3.0F, 1.6F, 0.4F})});
    meshHandles.push_back(hullMesh);
    for (std::uint32_t i = 0; i < Paper::kMaxPlayerSlots; ++i) {
      const auto e = scene.createEntity();
      scene.registry().emplace<Paper::MeshRenderer>(e, hullMesh, hullMaterial);
      scene.registry().get<Paper::Transform>(e).scale = glm::vec3(0.0F); // hidden
      remoteEntities.push_back(e);
    }
  }

  Paper::Camera camera;
  Paper::Camera viewCamera;
  camera.nearPlane = 1.0F;
  camera.farPlane = 8000.0F;
  camera.fovY = glm::radians(90.0F);

  MovementHud hud;
  renderer->setUiCallback([&] {
    overlay.draw(lastFrameSeconds);
    hud.setDetailsVisible(overlay.visible());
    hud.draw(player.state(), viewCamera, collision);
    if (session) {
      netHud.draw(*session, lastFrameSeconds, simulated.latencyMs);
      combatView->drawHud(*session);
    }
  });

  for (const auto &[name, value] : cvarOverrides) {
    require(Paper::cvars().set(name, value));
  }

  bool f1Held = false;
  bool rHeld = false;
  int frame = 0;
  int shotFrame = -1;
  double lastTime = window->time();
  while (!window->shouldClose()) {
    window->update();
    ++frame;
    if (screenshotPath != nullptr) {
      // Online, wait until the world has arrived.
      const bool ready = frame >= 30 && (!session || session->stats().snapshotsReceived > 40);
      if (ready && shotFrame < 0) {
        renderer->requestScreenshot(screenshotPath);
        shotFrame = frame;
      } else if (shotFrame >= 0 && frame > shotFrame) {
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
    const bool r = window->keyDown(Paper::Key::T);
    if (r && !rHeld && !session) {
      player.respawn(spawnOrigin, spawnYaw);
    }
    rHeld = r;

    const double now = window->time();
    lastFrameSeconds = static_cast<float>(now - lastTime);
    lastTime = now;

    if (conditioned) {
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - netStart)
                          .count();
      conditioned->update(ns);
    }
    {
      PAPER_ZONE("player update");
      player.update(*window, lastFrameSeconds, !overlay.visible());
    }
    if (session) {
      combatView->update(lastFrameSeconds, player.camera(camera), *session, collision);
      const auto remotes = session->remotePlayers();
      for (std::size_t i = 0; i < remoteEntities.size(); ++i) {
        auto &t = scene.registry().get<Paper::Transform>(remoteEntities[i]);
        const auto it = std::find_if(remotes.begin(), remotes.end(),
                                     [&](const Paper::NetPlayer &p) { return p.slot == i; });
        if (it == remotes.end() || !it->vitals.alive) {
          t.scale = glm::vec3(0.0F);
          continue;
        }
        t.scale = glm::vec3(1.0F);
        t.position = it->state.origin;
        t.rotation = glm::angleAxis(it->yaw, glm::vec3(0.0F, 1.0F, 0.0F));
      }
    }
    Paper::TransformSystem::update(scene);
    viewCamera = player.camera(camera);
    require(renderer->render(scene, viewCamera));
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
