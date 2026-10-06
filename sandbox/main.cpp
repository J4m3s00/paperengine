#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"

#include "fly_camera.h"

#include <spdlog/spdlog.h>

int main() {
  Paper::WindowConfig config{
      .width = 800, .height = 600, .title = "Paper Engine Window"};

  auto window = Paper::Window::create(config);
  if (!window) {
    spdlog::critical("{}", window.error().message);
    return 1;
  }
  auto renderer = Paper::Renderer::create(**window);
  if (!renderer) {
    spdlog::critical("{}", renderer.error().message);
    return 1;
  }

  auto cube = (*renderer)->createMesh(Paper::makeCube());
  if (!cube) {
    spdlog::critical("{}", cube.error().message);
    return 1;
  }

  // A spinning parent cube with an orbiting child that has its own child.
  Paper::Scene scene;
  const auto parent = scene.createEntity();
  scene.registry().emplace<Paper::MeshRenderer>(parent, *cube);

  const auto child = scene.createEntity();
  scene.registry().emplace<Paper::MeshRenderer>(child, *cube);
  scene.registry().get<Paper::Transform>(child).position = {2.0F, 0.0F, 0.0F};
  scene.registry().get<Paper::Transform>(child).scale = glm::vec3(0.5F);
  scene.setParent(child, parent);

  const auto grandchild = scene.createEntity();
  scene.registry().emplace<Paper::MeshRenderer>(grandchild, *cube);
  scene.registry().get<Paper::Transform>(grandchild).position = {0.0F, 1.5F, 0.0F};
  scene.registry().get<Paper::Transform>(grandchild).scale = glm::vec3(0.5F);
  scene.setParent(grandchild, child);

  FlyCamera flyCamera({3.0F, 2.5F, 5.0F}, {0.0F, 0.0F, 0.0F});

  double lastTime = (*window)->time();
  while (!(*window)->shouldClose()) {
    (*window)->update();
    if ((*window)->keyDown(Paper::Key::Escape)) {
      (*window)->requestClose();
    }

    const double now = (*window)->time();
    flyCamera.update(**window, static_cast<float>(now - lastTime));
    lastTime = now;

    const auto t = static_cast<float>(now);
    scene.registry().get<Paper::Transform>(parent).rotation =
        glm::angleAxis(t * 0.6F, glm::vec3(0.0F, 1.0F, 0.0F));
    scene.registry().get<Paper::Transform>(child).rotation =
        glm::angleAxis(t * 1.5F, glm::vec3(1.0F, 0.0F, 0.0F));
    Paper::TransformSystem::update(scene);

    if (auto rendered = (*renderer)->render(scene, flyCamera.camera()); !rendered) {
      spdlog::critical("{}", rendered.error().message);
      return 1;
    }
  }

  (*renderer)->destroyMesh(*cube);
  return 0;
}
