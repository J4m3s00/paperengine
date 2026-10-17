#include "local_player.h"
#include "audio/audio.h"
#include "combat_view.h"
#include "movement_hud.h"
#include "net_hud.h"
#include "mapview/material_palette.h"
#include "mapview/world_mesh.h"

#include "base/cvar.h"
#include "base/paths.h"
#include "base/profiler.h"
#include "core/debug/debug_overlay.h"
#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"
#include "game/hitbox.h"
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
#include <optional>
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

} // namespace

// Usage: client [--connect host[:port]] [--name N] [--offline] [--map map.json]
//               [--screenshot out.png] [--record out.replay.json]
//               [--set cvar value]... [--lag RTTms] [--jitter ms] [--loss 0..1]
// Without --connect or --offline the main menu opens.
namespace {
enum class Mode { Menu, Connecting, Online, Offline };

// A loaded map: collision, render meshes and the scene entities showing it.
struct World {
  Paper::MapData map;
  std::unique_ptr<Paper::CollisionWorld> collision;
  std::vector<Paper::MeshHandle> meshes;
  std::vector<Paper::MaterialHandle> materials;
  std::vector<entt::entity> entities;
  std::string name;
  glm::vec3 spawnOrigin{0.0F, 2.0F, 0.0F};
  float spawnYaw = 0.0F;
};

struct Endpoint {
  std::string host;
  std::uint16_t port = Paper::kDefaultServerPort;
};

std::optional<Endpoint> parseEndpoint(const std::string &text) {
  Endpoint e;
  const auto colon = text.find(':');
  e.host = text.substr(0, colon);
  if (e.host.empty()) {
    return std::nullopt;
  }
  if (colon != std::string::npos) {
    const int port = std::atoi(text.c_str() + colon + 1);
    if (port <= 0 || port > 65535) {
      return std::nullopt;
    }
    e.port = static_cast<std::uint16_t>(port);
  }
  return e;
}
} // namespace

int main(int argc, char **argv) {
  std::filesystem::path mapArg;
  const char *screenshotPath = nullptr;
  const char *recordPath = nullptr;
  std::vector<std::pair<std::string, std::string>> cvarOverrides;
  std::string connectTo;
  std::string playerName = "player";
  bool offlineArg = false;
  Paper::NetworkConditions simulated; // one-way, applied to both directions
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--map" && i + 1 < argc) {
      mapArg = argv[++i];
    } else if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (arg == "--record" && i + 1 < argc) {
      recordPath = argv[++i];
    } else if (arg == "--connect" && i + 1 < argc) {
      connectTo = argv[++i];
    } else if (arg == "--name" && i + 1 < argc) {
      playerName = argv[++i];
    } else if (arg == "--offline") {
      offlineArg = true;
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

  // Long-lived resources (not tied to a map).
  std::vector<Paper::MeshHandle> meshHandles;
  std::vector<Paper::MaterialHandle> materialHandles;
  const auto makeMaterial = [&](const glm::vec3 &color, const glm::vec3 &emissive, float roughness,
                                bool textured, std::vector<Paper::MaterialHandle> &owner) {
    const auto material = require(renderer->createMaterial(shader, {}));
    require(renderer->setMaterialParam(material, "baseColorFactor", glm::vec4(color, 1.0F)));
    require(renderer->setMaterialParam(material, "metallicFactor", 0.0F));
    require(renderer->setMaterialParam(material, "roughnessFactor", roughness));
    require(renderer->setMaterialParam(material, "emissiveFactor", emissive));
    require(renderer->setMaterialParam(material, "normalScale", 1.0F));
    require(renderer->setMaterialParam(material, "occlusionStrength", 1.0F));
    require(renderer->setMaterialParam(material, "alphaCutoff", 0.5F));
    if (textured) {
      require(renderer->setMaterialTexture(material, "baseColorTexture", grid));
    }
    owner.push_back(material);
    return material;
  };
  const auto fixedMaterial = [&](const glm::vec3 &color, const glm::vec3 &emissive) {
    return makeMaterial(color, emissive, 0.8F, false, materialHandles);
  };

  // Maps come and go: loading replaces the previous one.
  World world;
  const auto unloadWorld = [&] {
    for (const auto e : world.entities) {
      scene.registry().destroy(e);
    }
    for (const auto material : world.materials) {
      renderer->destroyMaterial(material);
    }
    for (const auto mesh : world.meshes) {
      renderer->destroyMesh(mesh);
    }
    world.entities.clear();
    world.materials.clear();
    world.meshes.clear();
  };
  const auto mapPathOf = [](const std::string &name) {
    const std::filesystem::path p(name);
    return p.has_parent_path() || p.extension() == ".json"
               ? p
               : Paper::dataDir("maps", PAPER_MAPS_DIR) / (name + ".json");
  };
  const auto loadWorld = [&](const std::string &name) -> std::expected<void, Paper::Error> {
    const auto path = mapPathOf(name);
    auto map = Paper::loadMap(path);
    if (!map) {
      return std::unexpected(map.error());
    }
    auto collision = map->buildCollision();
    if (!collision) {
      return std::unexpected(collision.error());
    }
    unloadWorld();
    world.map = std::move(*map);
    world.collision = std::make_unique<Paper::CollisionWorld>(std::move(*collision));
    world.name = path.stem().string();
    for (const MapMesh &part : buildMapMeshes(world.map)) {
      const auto mesh = require(renderer->createMesh(part.mesh));
      const auto material = makeMaterial(colorFor(part.material), glm::vec3(0.0F), 0.9F, true,
                                         world.materials);
      const auto entity = scene.createEntity();
      scene.registry().emplace<Paper::MeshRenderer>(entity, mesh, material);
      world.meshes.push_back(mesh);
      world.entities.push_back(entity);
    }
    world.spawnOrigin = glm::vec3(0.0F, 2.0F, 0.0F);
    world.spawnYaw = 0.0F;
    if (const auto spawns = world.map.entitiesOfType("spawn"); !spawns.empty()) {
      world.spawnOrigin = spawns.front().origin;
      world.spawnYaw = spawns.front().yaw;
    }
    spdlog::info("Loaded map '{}' ({} brushes, {} entities)", world.name, world.map.brushes.size(),
                 world.map.entities.size());
    return {};
  };
  require(loadWorld(mapArg.empty() ? "dev_world" : mapArg.string()));

  const Paper::WeaponDb weapons = require(Paper::WeaponDb::loadDirectory(Paper::dataDir("weapons", PAPER_WEAPONS_DIR)));
  Paper::MovementCvars movementCvars;
  Paper::DebugOverlay overlay;
  overlay.setVisible(screenshotPath != nullptr);
  float lastFrameSeconds = 0.0F;

  LocalPlayer player(*world.collision, movementCvars);
  if (recordPath != nullptr) {
    player.enableRecording(world.name + ".json");
  }
  player.respawn(world.spawnOrigin, world.spawnYaw);

  // Effects, HUD and remote players: built once, shown while online.
  std::unique_ptr<Paper::AudioEngine> audio;
  if (screenshotPath == nullptr) {
    if (auto engine = Paper::AudioEngine::create()) {
      audio = std::move(*engine);
    } else {
      spdlog::warn("audio disabled: {}", engine.error().message);
    }
  }
  Paper::MapData cubeMap;
  cubeMap.brushes.push_back({Paper::boxBrush(glm::vec3(-0.5F), glm::vec3(0.5F)), "cube"});
  const auto cubeMesh = require(renderer->createMesh(buildMapMeshes(cubeMap).front().mesh));
  meshHandles.push_back(cubeMesh);
  CombatView combatView(
      scene, CombatView::Assets{cubeMesh, fixedMaterial({0.15F, 0.15F, 0.17F}, glm::vec3(0.0F)),
                                fixedMaterial({0.1F, 0.1F, 0.1F}, {2.5F, 2.0F, 0.8F}),
                                fixedMaterial({0.1F, 0.1F, 0.1F}, {3.0F, 1.6F, 0.4F}),
                                fixedMaterial({0.1F, 0.1F, 0.1F}, {3.0F, 1.8F, 0.3F}),
                                fixedMaterial({0.1F, 0.1F, 0.1F}, {0.4F, 1.4F, 3.0F})});
  combatView.setAudio(audio.get());
  // Other players are drawn as their hitboxes, one colored box per hit group,
  // so what you see is exactly what you can hit.
  const glm::vec3 groupColors[] = {{0.9F, 0.15F, 0.15F},  {0.95F, 0.5F, 0.1F},  {0.95F, 0.85F, 0.2F},
                                   {0.2F, 0.5F, 0.95F},   {0.35F, 0.7F, 0.95F}, {0.2F, 0.75F, 0.3F},
                                   {0.45F, 0.85F, 0.4F}};
  Paper::MaterialHandle groupMaterials[7];
  for (int g = 0; g < 7; ++g) {
    groupMaterials[g] = fixedMaterial(groupColors[g] * 0.8F, groupColors[g] * 0.25F);
  }
  std::vector<entt::entity> remoteEntities;
  for (std::uint32_t slot = 0; slot < Paper::kMaxPlayerSlots; ++slot) {
    for (std::size_t g = 0; g < 7; ++g) {
      const auto e = scene.createEntity();
      scene.registry().emplace<Paper::MeshRenderer>(e, cubeMesh, groupMaterials[g]);
      scene.registry().get<Paper::Transform>(e).scale = glm::vec3(0.0F); // hidden
      remoteEntities.push_back(e);
    }
  }
  const auto hideRemotes = [&] {
    for (const auto e : remoteEntities) {
      scene.registry().get<Paper::Transform>(e).scale = glm::vec3(0.0F);
    }
    combatView.hideAll();
  };

  // Connection state.
  Mode mode = Mode::Menu;
  std::unique_ptr<Paper::EnetTransport> enet;
  std::unique_ptr<Paper::ConditionedTransport> conditioned;
  std::unique_ptr<Paper::ClientSession> session;
  Paper::PeerId serverPeer = 0;
  Paper::Transport *transport = nullptr;
  std::chrono::steady_clock::time_point connectStarted;
  std::chrono::steady_clock::time_point reconnectAt{};
  bool reconnectPending = false;
  std::string lastAddress = connectTo.empty() ? "127.0.0.1" : connectTo;
  std::string menuMessage;
  char hostBuf[128] = {};
  char nameBuf[32] = {};
  std::snprintf(hostBuf, sizeof hostBuf, "%s", lastAddress.c_str());
  std::snprintf(nameBuf, sizeof nameBuf, "%s", playerName.c_str());
  NetHud netHud;
  const auto netStart = std::chrono::steady_clock::now();
  const auto netNanos = [&] {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                netStart)
        .count();
  };

  const auto disconnect = [&](const std::string &message) {
    session.reset();
    conditioned.reset();
    enet.reset();
    transport = nullptr;
    player.setSession(nullptr);
    player.respawn(world.spawnOrigin, world.spawnYaw);
    hideRemotes();
    menuMessage = message;
    mode = Mode::Menu;
  };
  const auto startConnect = [&](const std::string &address) {
    const auto endpoint = parseEndpoint(address);
    if (!endpoint) {
      menuMessage = "Enter the server as host or host:port";
      return;
    }
    auto client = Paper::EnetTransport::createClient();
    if (!client) {
      menuMessage = client.error().message;
      return;
    }
    auto peer = (*client)->connect(endpoint->host, endpoint->port);
    if (!peer) {
      menuMessage = peer.error().message;
      return;
    }
    enet = std::move(*client);
    serverPeer = *peer;
    transport = enet.get();
    if (simulated.latencyMs > 0 || simulated.jitterMs > 0 || simulated.loss > 0) {
      conditioned = std::make_unique<Paper::ConditionedTransport>(*enet, simulated);
      transport = conditioned.get();
    }
    lastAddress = address;
    menuMessage.clear();
    connectStarted = std::chrono::steady_clock::now();
    mode = Mode::Connecting;
    spdlog::info("connecting to {}:{}", endpoint->host, endpoint->port);
  };
  const auto startOffline = [&] {
    if (const auto r = loadWorld(mapArg.empty() ? "dev_world" : mapArg.string()); !r) {
      menuMessage = r.error().message;
      return;
    }
    player.setWorld(*world.collision);
    player.setSession(nullptr);
    player.respawn(world.spawnOrigin, world.spawnYaw);
    mode = Mode::Offline;
  };

  // Connecting: wait for the server's Accept to learn its map, load it, then
  // hand the connection to a ClientSession.
  const auto pumpConnecting = [&] {
    if (conditioned) {
      conditioned->update(netNanos());
    }
    Paper::NetEvent event;
    while (transport->poll(event)) {
      if (event.type == Paper::NetEvent::Type::Connected) {
        transport->send(serverPeer, Paper::Channel::Reliable,
                        Paper::encode(Paper::HelloMsg{Paper::kProtocolVersion, playerName}));
      } else if (event.type == Paper::NetEvent::Type::Disconnected) {
        disconnect("Could not reach the server");
        return;
      } else if (event.type == Paper::NetEvent::Type::Data) {
        const auto type = Paper::peekMessageType(event.data);
        if (type && *type == Paper::MessageType::Reject) {
          const auto reject = Paper::decodeReject(event.data);
          disconnect("Rejected: " + (reject ? reject->reason : std::string("unknown reason")));
          return;
        }
        if (type && *type == Paper::MessageType::Accept) {
          const auto accept = Paper::decodeAccept(event.data);
          if (!accept) {
            disconnect("Bad answer from the server");
            return;
          }
          if (const auto r = loadWorld(accept->map); !r) {
            disconnect("Cannot load the server's map '" + accept->map + "': " + r.error().message);
            return;
          }
          if (accept->mapChecksum != 0 && accept->mapChecksum != Paper::mapChecksum(world.map)) {
            disconnect("Your copy of map '" + accept->map +
                       "' differs from the server's. Get the same map file.");
            return;
          }
          player.setWorld(*world.collision);
          session = std::make_unique<Paper::ClientSession>(*transport, serverPeer, *world.collision,
                                                           weapons, movementCvars.params(),
                                                           playerName);
          session->adoptAccept(*accept);
          player.setSession(session.get());
          mode = Mode::Online;
          return;
        }
      }
    }
    if (std::chrono::steady_clock::now() - connectStarted > std::chrono::seconds(6)) {
      disconnect("The server did not answer");
    }
  };

  if (!connectTo.empty()) {
    startConnect(connectTo);
  } else if (offlineArg || screenshotPath != nullptr) {
    startOffline();
  }

  Paper::Camera camera;
  Paper::Camera viewCamera;
  camera.nearPlane = 1.0F;
  camera.farPlane = 8000.0F;
  camera.fovY = glm::radians(90.0F);

  bool escHeld = false;
  bool mouseReleased = false;
  bool wantQuit = false;
  bool wantDisconnect = false; // handled outside the UI callback
  MovementHud hud;
  renderer->setUiCallback([&] {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (mode == Mode::Menu || mode == Mode::Connecting) {
      ImGui::SetNextWindowPos(ImVec2(display.x * 0.5F, display.y * 0.45F), ImGuiCond_Always,
                              ImVec2(0.5F, 0.5F));
      ImGui::Begin("Paper", nullptr,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoMove);
      if (mode == Mode::Connecting) {
        ImGui::Text("Connecting to %s ...", lastAddress.c_str());
        if (ImGui::Button("Cancel")) {
          wantDisconnect = true;
        }
      } else {
        ImGui::InputText("Server", hostBuf, sizeof hostBuf);
        ImGui::InputText("Name", nameBuf, sizeof nameBuf);
        if (ImGui::Button("Connect")) {
          playerName = nameBuf[0] != '\0' ? nameBuf : "player";
          startConnect(hostBuf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Offline sandbox")) {
          startOffline();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit")) {
          wantQuit = true;
        }
        if (!menuMessage.empty()) {
          ImGui::Separator();
          ImGui::TextWrapped("%s", menuMessage.c_str());
        }
      }
      ImGui::End();
      return;
    }
    overlay.draw(lastFrameSeconds);
    hud.setDetailsVisible(overlay.visible());
    hud.draw(player.state(), viewCamera, *world.collision);
    if (session) {
      netHud.draw(*session, lastFrameSeconds, simulated.latencyMs);
      combatView.drawHud(*session, window->keyDown(Paper::Key::Tab));
    }
    if (mouseReleased && !overlay.visible()) {
      ImGui::SetNextWindowPos(ImVec2(display.x * 0.5F, display.y * 0.5F), ImGuiCond_Always,
                              ImVec2(0.5F, 0.5F));
      ImGui::Begin("Menu", nullptr,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoMove);
      if (ImGui::Button("Resume")) {
        mouseReleased = false;
      }
      if (ImGui::Button(mode == Mode::Online ? "Disconnect" : "Main menu")) {
        mouseReleased = false;
        if (mode == Mode::Online) {
          wantDisconnect = true;
        } else {
          mode = Mode::Menu;
        }
      }
      if (ImGui::Button("Quit")) {
        wantQuit = true;
      }
      ImGui::End();
    }
  });

  for (const auto &[name, value] : cvarOverrides) {
    require(Paper::cvars().set(name, value));
  }

  bool f1Held = false;
  bool cursorWasFree = true;
  bool rHeld = false;
  int frame = 0;
  int shotFrame = -1;
  double lastTime = window->time();
  while (!window->shouldClose() && !wantQuit) {
    window->update();
    ++frame;
    if (screenshotPath != nullptr) {
      // Online, wait until the world has arrived.
      const bool ready = frame >= 30 && (mode == Mode::Offline || mode == Mode::Menu ||
                                         (session && session->stats().snapshotsReceived > 40));
      if (ready && shotFrame < 0) {
        renderer->requestScreenshot(screenshotPath);
        shotFrame = frame;
      } else if (shotFrame >= 0 && frame > shotFrame) {
        window->requestClose();
      }
    }
    // Escape frees the mouse and opens a small menu (the game keeps running);
    // clicking the game view grabs it again. The window's close button quits.
    const bool inGame = mode == Mode::Online || mode == Mode::Offline;
    const bool esc = window->keyDown(Paper::Key::Escape);
    if (inGame && esc && !escHeld) {
      mouseReleased = !mouseReleased;
    } else if (inGame && mouseReleased && !overlay.visible() &&
               window->mouseDown(Paper::MouseButton::Left) && !renderer->uiWantsMouse()) {
      mouseReleased = false;
    }
    escHeld = esc;

    // F1 toggles the debug overlay; with it open the cursor is free too.
    const bool f1 = window->keyDown(Paper::Key::F1);
    if (f1 && !f1Held) {
      overlay.toggle();
    }
    f1Held = f1;
    const bool cursorFree = !inGame || overlay.visible() || mouseReleased;
    if (screenshotPath == nullptr && cursorFree != cursorWasFree) {
      window->setCursorMode(cursorFree ? Paper::CursorMode::Normal : Paper::CursorMode::Captured);
      cursorWasFree = cursorFree;
    }
    const bool r = window->keyDown(Paper::Key::T);
    if (r && !rHeld && mode == Mode::Offline) {
      player.respawn(world.spawnOrigin, world.spawnYaw);
    }
    rHeld = r;

    const double now = window->time();
    lastFrameSeconds = static_cast<float>(now - lastTime);
    lastTime = now;

    if (wantDisconnect) {
      wantDisconnect = false;
      disconnect("");
    }
    if (mode == Mode::Connecting) {
      pumpConnecting();
    }
    if (conditioned && mode == Mode::Online) {
      conditioned->update(netNanos());
    }
    if (inGame) {
      PAPER_ZONE("player update");
      player.update(*window, lastFrameSeconds, !cursorFree);
    }
    if (mode == Mode::Online && session) {
      // The server can send us away (full, map change): go back, or follow it.
      if (session->status() == Paper::ClientSession::Status::Rejected ||
          session->status() == Paper::ClientSession::Status::Disconnected) {
        const bool mapChange = session->rejectReason() == "map change";
        disconnect(mapChange ? "Server is changing the map..."
                             : session->status() == Paper::ClientSession::Status::Rejected
                                   ? "Rejected: " + session->rejectReason()
                                   : "Disconnected from the server");
        if (mapChange) {
          reconnectPending = true;
          reconnectAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }
      }
    }
    if (reconnectPending && mode == Mode::Menu && std::chrono::steady_clock::now() >= reconnectAt) {
      reconnectPending = false;
      startConnect(lastAddress);
    }
    if (mode == Mode::Online && session) {
      combatView.update(lastFrameSeconds, player.camera(camera), *session, *world.collision);
      const auto remotes = session->remotePlayers();
      const Paper::FixedHitboxProvider hitboxProvider;
      for (std::size_t slot = 0; slot < Paper::kMaxPlayerSlots; ++slot) {
        const auto it = std::find_if(remotes.begin(), remotes.end(),
                                     [&](const Paper::NetPlayer &p) { return p.slot == slot; });
        Paper::HitboxSet set;
        const bool visible = it != remotes.end() && it->vitals.alive;
        if (visible) {
          hitboxProvider.hitboxes(Paper::poseOf(it->state, it->yaw, it->pitch), set);
        }
        for (std::size_t g = 0; g < 7; ++g) {
          auto &t = scene.registry().get<Paper::Transform>(remoteEntities[slot * 7 + g]);
          if (!visible || g >= set.count) {
            t.scale = glm::vec3(0.0F);
            continue;
          }
          const Paper::HitboxLocal &box = set.boxes[g];
          // Local (player yaw) frame -> world.
          const glm::quat yaw = glm::angleAxis(set.yaw, glm::vec3(0.0F, 1.0F, 0.0F));
          t.position = set.origin + yaw * ((box.mins + box.maxs) * 0.5F);
          t.rotation = yaw;
          t.scale = box.maxs - box.mins;
        }
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

  unloadWorld();
  for (const auto material : materialHandles) {
    renderer->destroyMaterial(material);
  }
  for (const auto mesh : meshHandles) {
    renderer->destroyMesh(mesh);
  }
  return 0;
}
