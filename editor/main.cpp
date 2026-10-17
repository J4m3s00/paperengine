// Paper map editor.
//
//   editor [map.json | map-name] [--screenshot out.png] [--select-brush N]
//
// Controls (viewport): hold the right mouse button to fly (WASD, Space up,
// Ctrl down, Shift fast, scroll changes the speed); left click selects; G
// grabs the selection and moves it on the ground with the mouse (click to
// drop, Esc to cancel); arrows and PageUp/PageDown nudge by the grid size;
// Delete removes; Ctrl+Z / Ctrl+Y undo and redo; Ctrl+S saves.
#include "mapview/material_palette.h"
#include "mapview/prop_models.h"
#include "mapview/world_mesh.h"

#include "base/paths.h"
#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"
#include "game/map_edit.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

glm::vec3 snapTo(const glm::vec3 &v, float grid) {
  return {std::round(v.x / grid) * grid, std::round(v.y / grid) * grid,
          std::round(v.z / grid) * grid};
}

glm::vec3 entityColor(const std::string &type) {
  if (type == "spawn_t") return {0.95F, 0.6F, 0.15F};
  if (type == "spawn_ct") return {0.3F, 0.55F, 0.95F};
  if (type == "spawn") return {0.3F, 0.85F, 0.35F};
  if (type == "bot") return {0.9F, 0.25F, 0.25F};
  return {0.7F, 0.7F, 0.7F};
}

class EditorApp {
public:
  EditorApp(Window &window, Renderer &renderer) : m_window(window), m_renderer(renderer) {
    m_shader = require(m_renderer.createShader({"pbr.vert.spv", "pbr.frag.spv"}));
    m_props = std::make_unique<PropModels>(m_renderer, m_scene, m_shader);
    m_editor.setPropExtents([this](const std::string &name) { return m_props->extents(name); });
    m_grid = require(m_renderer.createTexture(makeGrid(256, 4), ColorSpace::Srgb));
    m_scene.ambientLight = glm::vec3(0.25F, 0.26F, 0.3F);
    const auto sun = m_scene.createEntity();
    m_scene.registry().emplace<DirectionalLight>(sun);
    m_scene.registry().get<Transform>(sun).rotation = lookRotation(glm::vec3(-0.4F, -0.8F, -0.5F));

    MapData cube;
    cube.brushes.push_back(MapBrush::box(glm::vec3(-0.5F), glm::vec3(0.5F), "cube"));
    m_cube = require(m_renderer.createMesh(buildMapMeshes(cube).front().mesh));
    m_highlight = makeMaterial({1.0F, 0.55F, 0.1F}, {0.5F, 0.25F, 0.0F}, false);

    m_camera.nearPlane = 1.0F;
    m_camera.farPlane = 20000.0F;
    m_camera.fovY = glm::radians(75.0F);
    m_camera.position = {0.0F, 400.0F, 700.0F};
    m_pitch = -0.4F;
    m_renderer.setUiCallback([this] { drawUi(); });
  }

  ~EditorApp() {
    clearScene();
    m_props.reset();
    m_renderer.destroyMaterial(m_highlight);
    for (const auto m : m_materials) {
      m_renderer.destroyMaterial(m.second);
    }
    m_renderer.destroyMesh(m_cube);
  }

  void open(const std::string &nameOrPath) {
    const std::filesystem::path p(nameOrPath);
    const auto path = p.has_parent_path() || p.extension() == ".json"
                          ? p
                          : dataDir("maps", PAPER_MAPS_DIR) / (nameOrPath + ".json");
    auto map = loadMap(path);
    if (!map) {
      m_status = map.error().message;
      return;
    }
    m_editor.load(std::move(*map));
    m_path = path;
    m_status = "Opened " + path.string();
    m_issuesRevision = ~0ULL;
  }

  // For checking the editor without a display: render a few frames, save a
  // PNG, quit.
  void screenshotAndQuit(const char *path) { m_screenshot = path; }
  void selectInitially(Selection s) { m_editor.select(s); }

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
      updateCamera(dt);
      handleInput();
      if (m_editor.revision() != m_builtRevision || m_editor.selection() != m_builtSelection) {
        rebuildScene();
      }
      TransformSystem::update(m_scene);
      require(m_renderer.render(m_scene, cameraNow()));
    }
    if (m_editor.dirty()) {
      // Never lose work to a closed window: keep what was open next to the map.
      auto backup = m_path.empty() ? std::filesystem::path("unsaved_map.json") : m_path;
      backup += ".unsaved";
      if (saveMap(m_editor.map(), backup)) {
        spdlog::warn("closed with unsaved changes; saved a copy to {}", backup.string());
      }
    }
  }

private:
  // ----- scene building ---------------------------------------------------

  MaterialHandle makeMaterial(const glm::vec3 &color, const glm::vec3 &emissive, bool textured) {
    const auto material = require(m_renderer.createMaterial(m_shader, {}));
    require(m_renderer.setMaterialParam(material, "baseColorFactor", glm::vec4(color, 1.0F)));
    require(m_renderer.setMaterialParam(material, "metallicFactor", 0.0F));
    require(m_renderer.setMaterialParam(material, "roughnessFactor", 0.9F));
    require(m_renderer.setMaterialParam(material, "emissiveFactor", emissive));
    require(m_renderer.setMaterialParam(material, "normalScale", 1.0F));
    require(m_renderer.setMaterialParam(material, "occlusionStrength", 1.0F));
    require(m_renderer.setMaterialParam(material, "alphaCutoff", 0.5F));
    if (textured) {
      require(m_renderer.setMaterialTexture(material, "baseColorTexture", m_grid));
    }
    return material;
  }

  MaterialHandle entityMaterial(const std::string &type) {
    auto it = m_materials.find(type);
    if (it == m_materials.end()) {
      const glm::vec3 c = entityColor(type);
      it = m_materials.emplace(type, makeMaterial(c * 0.7F, c * 0.35F, false)).first;
    }
    return it->second;
  }

  void clearScene() {
    for (const auto e : m_entities) {
      m_scene.destroyTree(e);
    }
    for (const auto m : m_sceneMaterials) {
      m_renderer.destroyMaterial(m);
    }
    for (const auto m : m_sceneMeshes) {
      m_renderer.destroyMesh(m);
    }
    m_entities.clear();
    m_sceneMaterials.clear();
    m_sceneMeshes.clear();
  }

  void addMesh(const MeshData &data, MaterialHandle material, bool ownMaterial) {
    const auto mesh = require(m_renderer.createMesh(data));
    const auto e = m_scene.createEntity();
    m_scene.registry().emplace<MeshRenderer>(e, mesh, material);
    m_entities.push_back(e);
    m_sceneMeshes.push_back(mesh);
    if (ownMaterial) {
      m_sceneMaterials.push_back(material);
    }
  }

  void addBox(const glm::vec3 &position, const glm::quat &rotation, const glm::vec3 &scale,
              MaterialHandle material) {
    const auto e = m_scene.createEntity();
    m_scene.registry().emplace<MeshRenderer>(e, m_cube, material);
    auto &t = m_scene.registry().get<Transform>(e);
    t.position = position;
    t.rotation = rotation;
    t.scale = scale;
    m_entities.push_back(e);
  }

  // A wireframe box (12 thin bars) around [lo, hi].
  void addOutline(const glm::vec3 &lo, const glm::vec3 &hi) {
    constexpr float kBar = 1.5F;
    const glm::vec3 size = hi - lo;
    for (int axis = 0; axis < 3; ++axis) {
      for (int corner = 0; corner < 4; ++corner) {
        glm::vec3 at = lo;
        const int a = (axis + 1) % 3;
        const int b = (axis + 2) % 3;
        if ((corner & 1) != 0) at[a] += size[a];
        if ((corner & 2) != 0) at[b] += size[b];
        glm::vec3 center = at;
        center[axis] += size[axis] * 0.5F;
        glm::vec3 scale(kBar);
        scale[axis] = size[axis];
        addBox(center, glm::quat(1, 0, 0, 0), scale, m_highlight);
      }
    }
  }

  // Rebuilds everything shown: brushes per material, the selected brush in
  // the highlight color, and a marker per entity.
  void rebuildScene() {
    clearScene();
    const MapData &map = m_editor.map();
    const Selection sel = m_editor.selection();
    MapData rest;
    rest.entities.clear();
    for (std::size_t i = 0; i < map.brushes.size(); ++i) {
      if (sel.type == Selection::Type::Brush && sel.index == static_cast<int>(i)) {
        continue;
      }
      rest.brushes.push_back(map.brushes[i]);
    }
    for (const MapMesh &part : buildMapMeshes(rest)) {
      addMesh(part.mesh, makeMaterial(colorFor(part.material), glm::vec3(0.0F), true), true);
    }
    if (sel.type == Selection::Type::Brush) {
      MapData one;
      one.brushes.push_back(map.brushes[static_cast<std::size_t>(sel.index)]);
      for (const MapMesh &part : buildMapMeshes(one)) {
        addMesh(part.mesh, m_highlight, false);
      }
    }
    for (std::size_t i = 0; i < map.entities.size(); ++i) {
      const MapEntity &e = map.entities[i];
      const bool selected = sel.type == Selection::Type::Entity && sel.index == static_cast<int>(i);
      const MaterialHandle material = selected ? m_highlight : entityMaterial(e.type);
      const glm::quat yaw = glm::angleAxis(e.yaw, glm::vec3(0, 1, 0));
      addBox(e.origin + glm::vec3(0, 18, 0), yaw, {22.0F, 36.0F, 22.0F}, material);
      // A nub shows which way it faces (yaw 0 looks along -Z).
      addBox(e.origin + glm::vec3(0, 26, 0) + yaw * glm::vec3(0, 0, -20), yaw, {8.0F, 8.0F, 24.0F},
             material);
    }
    // Props: the real models; a selected one gets an outline.
    for (std::size_t i = 0; i < map.props.size(); ++i) {
      if (const entt::entity e = m_props->spawn(map.props[i]); e != entt::null) {
        m_entities.push_back(e);
      }
    }
    if (sel.type == Selection::Type::Prop) {
      if (const auto b = m_editor.bounds(sel)) {
        addOutline(b->first, b->second);
      }
    }
    m_builtRevision = m_editor.revision();
    m_builtSelection = sel;
  }

  // ----- camera and picking ----------------------------------------------

  [[nodiscard]] glm::quat rotation() const {
    return glm::angleAxis(m_yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(m_pitch, glm::vec3(1, 0, 0));
  }
  [[nodiscard]] Camera cameraNow() const {
    Camera c = m_camera;
    c.rotation = rotation();
    return c;
  }

  void updateCamera(float dt) {
    const bool wantFly = m_window.mouseDown(MouseButton::Right) && !m_renderer.uiWantsMouse();
    if (wantFly != m_flying) {
      m_flying = wantFly;
      m_window.setCursorMode(m_flying ? CursorMode::Captured : CursorMode::Normal);
    }
    if (!m_flying) {
      return;
    }
    const glm::dvec2 d = m_window.mouseDelta();
    m_yaw -= static_cast<float>(d.x) * 0.0025F;
    m_pitch = std::clamp(m_pitch - static_cast<float>(d.y) * 0.0025F, -1.55F, 1.55F);
    m_flySpeed = std::clamp(m_flySpeed * std::pow(1.15F, static_cast<float>(m_window.scrollDelta().y)),
                            50.0F, 6000.0F);
    const glm::quat q = rotation();
    glm::vec3 move(0.0F);
    if (m_window.keyDown(Key::W)) move += q * glm::vec3(0, 0, -1);
    if (m_window.keyDown(Key::S)) move += q * glm::vec3(0, 0, 1);
    if (m_window.keyDown(Key::D)) move += q * glm::vec3(1, 0, 0);
    if (m_window.keyDown(Key::A)) move += q * glm::vec3(-1, 0, 0);
    if (m_window.keyDown(Key::Space)) move.y += 1.0F;
    if (m_window.keyDown(Key::LeftControl)) move.y -= 1.0F;
    if (glm::length(move) > 0.0F) {
      const float speed = m_flySpeed * (m_window.keyDown(Key::LeftShift) ? 3.0F : 1.0F);
      m_camera.position += glm::normalize(move) * speed * dt;
    }
  }

  // A ray from the camera through the mouse cursor.
  [[nodiscard]] std::pair<glm::vec3, glm::vec3> mouseRay() const {
    const glm::dvec2 m = m_window.mousePosition();
    const float w = static_cast<float>(std::max(1, m_window.width()));
    const float h = static_cast<float>(std::max(1, m_window.height()));
    const float nx = static_cast<float>(m.x) / w * 2.0F - 1.0F;
    const float ny = 1.0F - static_cast<float>(m.y) / h * 2.0F;
    const float t = std::tan(m_camera.fovY * 0.5F);
    const glm::vec3 local(nx * t * (w / h), ny * t, -1.0F);
    return {m_camera.position, glm::normalize(rotation() * local)};
  }

  // Where a new object goes: in front of the camera, on whatever it looks at.
  [[nodiscard]] glm::vec3 spawnPoint() const {
    const glm::vec3 fwd = rotation() * glm::vec3(0, 0, -1);
    const auto hit = m_editor.pick(m_camera.position, fwd, 1500.0F);
    const glm::vec3 p = m_camera.position + fwd * (hit ? hit->distance : 400.0F);
    return snapTo(p, m_gridSize);
  }

  // ----- input ------------------------------------------------------------

  void handleInput() {
    const bool ctrl = m_window.keyDown(Key::LeftControl) && !m_flying;
    const auto pressed = [&](Key key, bool &held) {
      const bool down = m_window.keyDown(key);
      const bool edge = down && !held;
      held = down;
      return edge && !m_renderer.uiWantsKeyboard();
    };
    if (ctrl && pressed(Key::Z, m_zHeld)) m_editor.undo();
    if (ctrl && pressed(Key::Y, m_yHeld)) m_editor.redo();
    if (ctrl && pressed(Key::S, m_sHeld)) save();
    if (pressed(Key::Delete, m_delHeld) && !m_grabbing) m_editor.remove(m_editor.selection());
    if (pressed(Key::G, m_gHeld) && !m_flying && m_editor.selection().valid()) {
      m_grabbing ? finishGrab() : beginGrab();
    }
    const bool escape = pressed(Key::Escape, m_escHeld);
    if (escape && m_grabbing) cancelGrab();

    if (!m_grabbing && !m_flying) {
      const glm::vec3 nudges[] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, -1, 0}};
      const Key keys[] = {Key::Left, Key::Right, Key::Up, Key::Down, Key::PageUp, Key::PageDown};
      for (int i = 0; i < 6; ++i) {
        if (pressed(keys[i], m_nudgeHeld[i])) {
          m_editor.move(m_editor.selection(), nudges[i] * m_gridSize);
        }
      }
    }

    const bool click = m_window.mouseDown(MouseButton::Left);
    const bool clickEdge = click && !m_clickHeld;
    m_clickHeld = click;
    if (m_grabbing) {
      updateGrab();
      if (clickEdge) finishGrab();
    } else if (clickEdge && !m_renderer.uiWantsMouse() && !m_flying) {
      const auto [origin, dir] = mouseRay();
      const auto hit = m_editor.pick(origin, dir, 20000.0F);
      m_editor.select(hit ? hit->what : Selection{});
    }
  }

  void beginGrab() {
    const auto b = m_editor.bounds(m_editor.selection());
    if (!b) return;
    const auto [origin, dir] = mouseRay();
    m_grabY = (b->first.y + b->second.y) * 0.5F;
    m_grabAnchor = b->first;
    // Where on the ground plane the cursor is, relative to the object corner.
    const auto p = intersectGround(origin, dir, m_grabY);
    m_grabOffset = p ? *p - b->first : glm::vec3(0.0F);
    m_grabbing = true;
    m_editor.beginGroup();
    m_grabMoved = false;
  }

  [[nodiscard]] static std::optional<glm::vec3> intersectGround(const glm::vec3 &o, const glm::vec3 &d,
                                                                float y) {
    if (std::fabs(d.y) < 1e-4F) return std::nullopt;
    const float t = (y - o.y) / d.y;
    if (t < 0.0F || t > 30000.0F) return std::nullopt;
    return o + d * t;
  }

  void updateGrab() {
    const auto b = m_editor.bounds(m_editor.selection());
    if (!b) return;
    const auto [origin, dir] = mouseRay();
    const auto p = intersectGround(origin, dir, m_grabY);
    if (!p) return;
    const glm::vec3 target = snapTo(*p - m_grabOffset, m_gridSize);
    glm::vec3 delta = target - b->first;
    delta.y = 0.0F;
    if (delta != glm::vec3(0.0F)) {
      m_editor.move(m_editor.selection(), delta);
      m_grabMoved = true;
    }
  }

  void finishGrab() {
    m_editor.endGroup();
    m_grabbing = false;
  }

  void cancelGrab() {
    m_editor.endGroup();
    m_grabbing = false;
    if (m_grabMoved) {
      m_editor.undo();
    }
  }

  // ----- files ------------------------------------------------------------

  void save() {
    if (m_path.empty()) {
      m_openSaveAs = true;
      return;
    }
    saveTo(m_path);
  }

  void saveTo(const std::filesystem::path &path) {
    for (const MapIssue &issue : m_editor.validate()) {
      if (issue.error) {
        m_status = "Not saved: " + issue.text;
        return;
      }
    }
    if (const auto r = saveMap(m_editor.map(), path); !r) {
      m_status = r.error().message;
      return;
    }
    m_path = path;
    m_editor.markSaved();
    m_status = "Saved " + path.string();
  }

  void playtest() {
    auto path = m_path;
    if (path.empty() || m_editor.dirty()) {
      path = std::filesystem::temp_directory_path() / "paper_playtest.json";
      if (const auto r = saveMap(m_editor.map(), path); !r) {
        m_status = r.error().message;
        return;
      }
    }
    const auto exe = executableDir();
    std::filesystem::path client = exe / "client";
    for (const auto &candidate : {exe / "client", exe / ".." / "client" / "client",
                                  exe / "client.exe", exe / ".." / "client" / "client.exe"}) {
      if (std::filesystem::exists(candidate)) {
        client = candidate;
        break;
      }
    }
#if defined(_WIN32)
    const std::string command = "start \"\" \"" + client.string() + "\" --offline --map \"" +
                                path.string() + "\"";
#else
    const std::string command = "\"" + client.string() + "\" --offline --map \"" + path.string() +
                                "\" > /dev/null 2>&1 &";
#endif
    const int rc = std::system(command.c_str());
    m_status = rc == 0 ? "Started the game on " + path.string() : "Could not start " + client.string();
  }

  // ----- UI ---------------------------------------------------------------

  bool dragVec3(const char *label, glm::vec3 &v, float speed) {
    float a[3] = {v.x, v.y, v.z};
    const bool changed = ImGui::DragFloat3(label, a, speed);
    trackEditGroup();
    if (changed) v = {a[0], a[1], a[2]};
    return changed;
  }
  bool dragFloat(const char *label, float &v, float speed) {
    const bool changed = ImGui::DragFloat(label, &v, speed);
    trackEditGroup();
    return changed;
  }
  // A drag of a field is one undo step: group from press to release.
  void trackEditGroup() {
    if (ImGui::IsItemActivated()) m_editor.beginGroup();
    if (ImGui::IsItemDeactivated()) m_editor.endGroup();
  }

  void drawMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        m_editor.load(MapData{});
        m_path.clear();
        m_status = "New map";
      }
      if (ImGui::MenuItem("Open...")) m_openDialog = true;
      if (ImGui::MenuItem("Save", "Ctrl+S")) save();
      if (ImGui::MenuItem("Save as...")) m_openSaveAs = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Playtest in game")) playtest();
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) m_quit = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_editor.canUndo())) m_editor.undo();
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_editor.canRedo())) m_editor.redo();
      ImGui::Separator();
      if (ImGui::MenuItem("Duplicate", nullptr, false, m_editor.selection().valid())) {
        m_editor.duplicate(m_editor.selection(), {m_gridSize * 2, 0, m_gridSize * 2});
      }
      if (ImGui::MenuItem("Delete", "Del", false, m_editor.selection().valid())) {
        m_editor.remove(m_editor.selection());
      }
      if (ImGui::MenuItem("Grab / move", "G", false, m_editor.selection().valid())) {
        m_grabbing ? finishGrab() : beginGrab();
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add")) {
      const glm::vec3 p = spawnPoint();
      if (ImGui::MenuItem("Box 64")) m_editor.addBox(p, p + glm::vec3(64.0F), "box");
      if (ImGui::MenuItem("Floor slab")) {
        m_editor.addBox(p + glm::vec3(-256, -16, -256), p + glm::vec3(256, 0, 256), "floor");
      }
      if (ImGui::MenuItem("Wall")) {
        m_editor.addBox(p + glm::vec3(-128, 0, -8), p + glm::vec3(128, 128, 8), "wall");
      }
      if (ImGui::MenuItem("Ramp")) {
        m_editor.addRamp({{p.x, p.z}, {p.x + 128, p.z + 128}, p.y, p.y + 64, p.y - 16, "+x"}, "ramp");
      }
      ImGui::Separator();
      for (const char *type : kEntityTypes) {
        if (ImGui::MenuItem(type)) m_editor.addEntity(type, p + glm::vec3(0, 2, 0));
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      static const float sizes[] = {1, 2, 4, 8, 16, 32, 64, 128};
      for (const float s : sizes) {
        char label[32];
        std::snprintf(label, sizeof label, "Grid %g", static_cast<double>(s));
        if (ImGui::MenuItem(label, nullptr, m_gridSize == s)) m_gridSize = s;
      }
      ImGui::EndMenu();
    }
    ImGui::Text("   %s%s   grid %g   %s", m_path.empty() ? "(unsaved map)" : m_path.filename().string().c_str(),
                m_editor.dirty() ? " *" : "", static_cast<double>(m_gridSize),
                m_grabbing ? "GRAB: click to drop, Esc cancels" : "");
    ImGui::EndMainMenuBar();
  }

  void drawDialogs() {
    if (m_openDialog) {
      ImGui::OpenPopup("Open map");
      m_openDialog = false;
      m_mapFiles.clear();
      std::error_code ec;
      for (const auto &entry : std::filesystem::directory_iterator(dataDir("maps", PAPER_MAPS_DIR), ec)) {
        if (entry.path().extension() == ".json") m_mapFiles.push_back(entry.path());
      }
      std::sort(m_mapFiles.begin(), m_mapFiles.end());
    }
    if (ImGui::BeginPopupModal("Open map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      for (const auto &file : m_mapFiles) {
        if (ImGui::Selectable(file.filename().string().c_str())) {
          open(file.string());
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::InputText("path", m_pathBuf, sizeof m_pathBuf);
      if (ImGui::Button("Open path")) {
        open(m_pathBuf);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    if (m_openSaveAs) {
      ImGui::OpenPopup("Save map as");
      m_openSaveAs = false;
      const auto suggestion = m_path.empty() ? dataDir("maps", PAPER_MAPS_DIR) / "new_map.json" : m_path;
      std::snprintf(m_pathBuf, sizeof m_pathBuf, "%s", suggestion.string().c_str());
    }
    if (ImGui::BeginPopupModal("Save map as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::InputText("path", m_pathBuf, sizeof m_pathBuf);
      if (ImGui::Button("Save")) {
        saveTo(m_pathBuf);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  void drawOutliner() {
    ImGui::SetNextWindowPos(ImVec2(10, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Outliner");
    const MapData &map = m_editor.map();
    const Selection sel = m_editor.selection();
    if (ImGui::CollapsingHeader("Brushes", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (std::size_t i = 0; i < map.brushes.size(); ++i) {
        const MapBrush &b = map.brushes[i];
        const char *kind = b.kind == BrushKind::Box ? "box" : b.kind == BrushKind::Ramp ? "ramp" : "planes";
        char label[96];
        std::snprintf(label, sizeof label, "%zu  %s  %s", i, kind, b.material.empty() ? "-" : b.material.c_str());
        const bool selected = sel.type == Selection::Type::Brush && sel.index == static_cast<int>(i);
        if (ImGui::Selectable(label, selected)) m_editor.select({Selection::Type::Brush, static_cast<int>(i)});
        if (selected && m_scrollToSelection) ImGui::SetScrollHereY();
      }
    }
    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (std::size_t i = 0; i < map.entities.size(); ++i) {
        char label[96];
        std::snprintf(label, sizeof label, "%zu  %s", i, map.entities[i].type.c_str());
        const bool selected = sel.type == Selection::Type::Entity && sel.index == static_cast<int>(i);
        if (ImGui::Selectable(label, selected)) m_editor.select({Selection::Type::Entity, static_cast<int>(i)});
      }
    }
    if (ImGui::CollapsingHeader("Props", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (std::size_t i = 0; i < map.props.size(); ++i) {
        char label[96];
        std::snprintf(label, sizeof label, "%zu  %s", i, map.props[i].model.c_str());
        const bool selected = sel.type == Selection::Type::Prop && sel.index == static_cast<int>(i);
        if (ImGui::Selectable(label, selected)) m_editor.select({Selection::Type::Prop, static_cast<int>(i)});
      }
    }
    m_scrollToSelection = false;
    ImGui::End();
  }

  // The kit's pieces: click one to place it where the camera looks.
  void drawCatalog() {
    ImGui::SetNextWindowPos(ImVec2(10, 780), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("Prop kit");
    ImGui::InputText("filter", m_propFilter, sizeof m_propFilter);
    ImGui::BeginChild("pieces");
    for (const std::string &name : m_props->catalog()) {
      if (m_propFilter[0] != '\0' && name.find(m_propFilter) == std::string::npos) continue;
      if (ImGui::Selectable(name.c_str())) {
        m_editor.addProp(name, spawnPoint());
      }
    }
    ImGui::EndChild();
    ImGui::End();
  }

  void drawProperties() {
    ImGui::SetNextWindowPos(ImVec2(10, 470), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Properties");
    const Selection sel = m_editor.selection();
    const MapData &map = m_editor.map();
    if (!sel.valid()) {
      ImGui::TextDisabled("Nothing selected.\nClick something in the viewport\nor pick it in the outliner.");
    } else if (sel.type == Selection::Type::Brush) {
      MapBrush b = map.brushes[static_cast<std::size_t>(sel.index)];
      ImGui::Text("Brush %d", sel.index);
      if (ImGui::BeginCombo("material", b.material.empty() ? "-" : b.material.c_str())) {
        for (const std::string &name : materialNames()) {
          if (ImGui::Selectable(name.c_str(), b.material == name)) m_editor.setMaterial(sel.index, name);
        }
        ImGui::EndCombo();
      }
      if (b.kind == BrushKind::Box) {
        bool changed = dragVec3("mins", b.mins, m_gridSize * 0.25F);
        changed = dragVec3("maxs", b.maxs, m_gridSize * 0.25F) || changed;
        if (changed) {
          if (const auto r = m_editor.setBox(sel.index, b.mins, b.maxs); !r) m_status = r.error().message;
        }
        const glm::vec3 size = b.maxs - b.mins;
        ImGui::TextDisabled("size %g x %g x %g", static_cast<double>(size.x), static_cast<double>(size.y),
                            static_cast<double>(size.z));
      } else if (b.kind == BrushKind::Ramp) {
        RampDef r = b.ramp;
        bool changed = false;
        float lo[2] = {r.min.x, r.min.y};
        float hi[2] = {r.max.x, r.max.y};
        if (ImGui::DragFloat2("min x,z", lo, m_gridSize * 0.25F)) { r.min = {lo[0], lo[1]}; changed = true; }
        trackEditGroup();
        if (ImGui::DragFloat2("max x,z", hi, m_gridSize * 0.25F)) { r.max = {hi[0], hi[1]}; changed = true; }
        trackEditGroup();
        changed = dragFloat("low", r.low, m_gridSize * 0.25F) || changed;
        changed = dragFloat("high", r.high, m_gridSize * 0.25F) || changed;
        changed = dragFloat("bottom", r.bottom, m_gridSize * 0.25F) || changed;
        static const char *dirs[] = {"+x", "-x", "+z", "-z"};
        if (ImGui::BeginCombo("rises toward", r.rising.c_str())) {
          for (const char *d : dirs) {
            if (ImGui::Selectable(d, r.rising == d)) { r.rising = d; changed = true; }
          }
          ImGui::EndCombo();
        }
        if (changed) {
          if (const auto res = m_editor.setRamp(sel.index, r); !res) m_status = res.error().message;
        }
      } else {
        ImGui::TextDisabled("Free-form plane brush (%zu planes):\nmove and delete work, shape editing\nis for boxes and ramps.",
                            b.brush.planes.size());
      }
      if (ImGui::Button("Duplicate")) m_editor.duplicate(sel, {m_gridSize * 2, 0, m_gridSize * 2});
      ImGui::SameLine();
      if (ImGui::Button("Delete")) m_editor.remove(sel);
      ImGui::SameLine();
      if (ImGui::Button("Focus")) focus(sel);
    } else if (sel.type == Selection::Type::Prop) {
      MapProp p = map.props[static_cast<std::size_t>(sel.index)];
      ImGui::Text("Prop %d", sel.index);
      std::string model = p.model;
      if (ImGui::BeginCombo("model", model.c_str())) {
        for (const std::string &name : m_props->catalog()) {
          if (ImGui::Selectable(name.c_str(), model == name)) model = name;
        }
        ImGui::EndCombo();
      }
      bool changed = model != p.model;
      changed = dragVec3("origin", p.origin, m_gridSize * 0.25F) || changed;
      float degrees = glm::degrees(p.yaw);
      if (ImGui::SliderFloat("yaw", &degrees, -180.0F, 180.0F, "%.0f deg")) {
        p.yaw = glm::radians(degrees);
        changed = true;
      }
      trackEditGroup();
      changed = dragFloat("scale", p.scale, 0.01F) || changed;
      if (changed) m_editor.setProp(sel.index, model, p.origin, p.yaw, p.scale);
      if (ImGui::Button("Add collision box")) {
        if (m_editor.addPropCollision(sel.index) < 0) m_status = "Unknown model: no collision box";
      }
      ImGui::SameLine();
      if (ImGui::Button("Duplicate")) m_editor.duplicate(sel, {m_gridSize * 2, 0, m_gridSize * 2});
      if (ImGui::Button("Delete")) m_editor.remove(sel);
      ImGui::SameLine();
      if (ImGui::Button("Focus")) focus(sel);
    } else {
      MapEntity e = map.entities[static_cast<std::size_t>(sel.index)];
      ImGui::Text("Entity %d", sel.index);
      std::string type = e.type;
      if (ImGui::BeginCombo("type", type.c_str())) {
        for (const char *t : kEntityTypes) {
          if (ImGui::Selectable(t, type == t)) type = t;
        }
        ImGui::EndCombo();
      }
      bool changed = type != e.type;
      changed = dragVec3("origin", e.origin, m_gridSize * 0.25F) || changed;
      float degrees = glm::degrees(e.yaw);
      if (ImGui::SliderFloat("yaw", &degrees, -180.0F, 180.0F, "%.0f deg")) {
        e.yaw = glm::radians(degrees);
        changed = true;
      }
      trackEditGroup();
      if (changed) m_editor.setEntity(sel.index, type, e.origin, e.yaw);
      if (ImGui::Button("Duplicate")) m_editor.duplicate(sel, {m_gridSize * 2, 0, m_gridSize * 2});
      ImGui::SameLine();
      if (ImGui::Button("Delete")) m_editor.remove(sel);
      ImGui::SameLine();
      if (ImGui::Button("Focus")) focus(sel);
    }
    ImGui::End();
  }

  void focus(Selection s) {
    if (const auto b = m_editor.bounds(s)) {
      const glm::vec3 center = (b->first + b->second) * 0.5F;
      const float radius = std::max(60.0F, glm::length(b->second - b->first) * 0.8F);
      m_camera.position = center - rotation() * glm::vec3(0, 0, -1) * (radius * 2.0F);
    }
  }

  void drawIssues() {
    if (m_issuesRevision != m_editor.revision()) {
      m_issues = m_editor.validate();
      m_issuesRevision = m_editor.revision();
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x - 10, 40), ImGuiCond_FirstUseEver, ImVec2(1, 0));
    ImGui::SetNextWindowSize(ImVec2(340, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Checks");
    if (m_issues.empty()) {
      ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1), "No problems found.");
    }
    for (const MapIssue &i : m_issues) {
      ImGui::TextColored(i.error ? ImVec4(1, 0.4F, 0.3F, 1) : ImVec4(1, 0.85F, 0.3F, 1), "%s %s",
                         i.error ? "error:" : "warning:", i.text.c_str());
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("%zu brushes, %zu entities", m_editor.map().brushes.size(),
                        m_editor.map().entities.size());
    ImGui::End();
  }

  void drawUi() {
    drawMenu();
    drawOutliner();
    drawCatalog();
    drawProperties();
    drawIssues();
    drawDialogs();
  }

  Window &m_window;
  Renderer &m_renderer;
  Scene m_scene;
  Camera m_camera;
  float m_yaw = 0.0F;
  float m_pitch = 0.0F;
  float m_flySpeed = 600.0F;
  bool m_flying = false;

  ShaderHandle m_shader;
  TextureHandle m_grid;
  MeshHandle m_cube;
  MaterialHandle m_highlight;
  std::map<std::string, MaterialHandle> m_materials; // entity colors, long-lived
  std::vector<entt::entity> m_entities;
  std::vector<MaterialHandle> m_sceneMaterials;
  std::vector<MeshHandle> m_sceneMeshes;
  std::unique_ptr<PropModels> m_props;
  char m_propFilter[64] = {};
  std::uint64_t m_builtRevision = ~0ULL;
  Selection m_builtSelection{Selection::Type::Brush, -2};

  MapEditor m_editor;
  std::filesystem::path m_path;
  std::string m_status = "Right mouse button + WASD to fly, click to select.";
  std::vector<MapIssue> m_issues;
  std::uint64_t m_issuesRevision = ~0ULL;
  float m_gridSize = 16.0F;

  bool m_grabbing = false;
  bool m_grabMoved = false;
  float m_grabY = 0.0F;
  glm::vec3 m_grabAnchor{0.0F};
  glm::vec3 m_grabOffset{0.0F};

  bool m_clickHeld = false;
  bool m_zHeld = false, m_yHeld = false, m_sHeld = false, m_delHeld = false, m_gHeld = false,
       m_escHeld = false;
  bool m_nudgeHeld[6] = {};
  bool m_quit = false;
  const char *m_screenshot = nullptr;
  bool m_openDialog = false;
  bool m_openSaveAs = false;
  bool m_scrollToSelection = false;
  char m_pathBuf[512] = {};
  std::vector<std::filesystem::path> m_mapFiles;
};
} // namespace

int main(int argc, char **argv) {
  auto window = require(Window::create({.width = 1600, .height = 900, .title = "Paper Editor"}));
  auto renderer = require(Renderer::create(*window));
  EditorApp app(*window, *renderer);
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      app.screenshotAndQuit(argv[++i]);
    } else if (arg == "--select-brush" && i + 1 < argc) {
      app.selectInitially({Selection::Type::Brush, std::atoi(argv[++i])});
    } else if (arg == "--select-entity" && i + 1 < argc) {
      app.selectInitially({Selection::Type::Entity, std::atoi(argv[++i])});
    } else {
      app.open(arg);
    }
  }
  app.run();
  return 0;
}
