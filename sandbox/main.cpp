#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"

#include "fly_camera.h"

#include <spdlog/spdlog.h>

#include <cmath>

namespace {
// Logs the error and exits; sandbox treats any engine error as fatal.
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

int main() {
  Paper::WindowConfig config{
      .width = 800, .height = 600, .title = "Paper Engine Window"};

  auto window = require(Paper::Window::create(config));
  auto renderer = require(Paper::Renderer::create(*window));

  const auto cube = require(renderer->createMesh(Paper::makeCube()));
  const auto shader = require(
      renderer->createShader({"mesh.vert.spv", "mesh.frag.spv"}));

  // Three materials sharing one shader but differing in parameters and state.
  const auto red = require(renderer->createMaterial(shader));
  require(renderer->setMaterialParam(red, "baseColor", glm::vec4(0.9F, 0.25F, 0.2F, 1.0F)));
  require(renderer->setMaterialParam(red, "normalMix", 0.0F));

  const auto green = require(renderer->createMaterial(shader));
  require(renderer->setMaterialParam(green, "baseColor", glm::vec4(0.2F, 0.8F, 0.4F, 1.0F)));
  require(renderer->setMaterialParam(green, "normalMix", 0.35F));

  // Alpha-masked and double-sided: the pattern comes from the cutoff changing
  // over time (see the loop).
  const auto masked = require(renderer->createMaterial(
      shader, {.cullMode = Paper::CullMode::None,
               .alphaMode = Paper::AlphaMode::Mask}));
  require(renderer->setMaterialParam(masked, "baseColor", glm::vec4(1.0F, 0.85F, 0.2F, 0.5F)));
  require(renderer->setMaterialParam(masked, "normalMix", 0.0F));
  require(renderer->setMaterialParam(masked, "alphaCutoff", 0.25F));

  // A spinning parent cube with an orbiting child that has its own child.
  Paper::Scene scene;
  const auto parent = scene.createEntity();
  scene.registry().emplace<Paper::MeshRenderer>(parent, cube, red);

  const auto child = scene.createEntity();
  scene.registry().emplace<Paper::MeshRenderer>(child, cube, green);
  scene.registry().get<Paper::Transform>(child).position = {2.0F, 0.0F, 0.0F};
  scene.registry().get<Paper::Transform>(child).scale = glm::vec3(0.5F);
  scene.setParent(child, parent);

  const auto grandchild = scene.createEntity();
  scene.registry().emplace<Paper::MeshRenderer>(grandchild, cube, masked);
  scene.registry().get<Paper::Transform>(grandchild).position = {0.0F, 1.5F, 0.0F};
  scene.registry().get<Paper::Transform>(grandchild).scale = glm::vec3(0.5F);
  scene.setParent(grandchild, child);

  FlyCamera flyCamera({3.0F, 2.5F, 5.0F}, {0.0F, 0.0F, 0.0F});

  double lastTime = window->time();
  while (!window->shouldClose()) {
    window->update();
    if (window->keyDown(Paper::Key::Escape)) {
      window->requestClose();
    }

    const double now = window->time();
    flyCamera.update(*window, static_cast<float>(now - lastTime));
    lastTime = now;

    const auto t = static_cast<float>(now);
    scene.registry().get<Paper::Transform>(parent).rotation =
        glm::angleAxis(t * 0.6F, glm::vec3(0.0F, 1.0F, 0.0F));
    scene.registry().get<Paper::Transform>(child).rotation =
        glm::angleAxis(t * 1.5F, glm::vec3(1.0F, 0.0F, 0.0F));
    Paper::TransformSystem::update(scene);

    // Parameters can change at runtime; each frame in flight gets its own copy.
    require(renderer->setMaterialParam(red, "normalMix", 0.5F + 0.5F * std::sin(t)));
    require(renderer->setMaterialParam(masked, "alphaCutoff", 0.5F + 0.5F * std::sin(t * 2.0F)));

    require(renderer->render(scene, flyCamera.camera()));
  }

  renderer->destroyMaterial(red);
  renderer->destroyMaterial(green);
  renderer->destroyMaterial(masked);
  renderer->destroyShader(shader);
  renderer->destroyMesh(cube);
  return 0;
}
