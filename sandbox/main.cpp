#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"

#include "fly_camera.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

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

// Procedural test images so the sandbox needs no asset files.
Paper::Image makeChecker(std::uint32_t size, std::uint32_t cell) {
  Paper::Image image{size, size, std::vector<std::uint8_t>(size * size * 4)};
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const bool odd = ((x / cell) + (y / cell)) % 2 == 1;
      const std::uint8_t value = odd ? 70 : 255;
      std::uint8_t *px = &image.pixels[(y * size + x) * 4];
      px[0] = px[1] = px[2] = value;
      px[3] = 255;
    }
  }
  return image;
}

// Opaque dots on a transparent background; the alpha-mask cutoff animates them.
Paper::Image makeDots(std::uint32_t size, std::uint32_t cell) {
  Paper::Image image{size, size, std::vector<std::uint8_t>(size * size * 4)};
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const float dx = static_cast<float>(x % cell) / static_cast<float>(cell) - 0.5F;
      const float dy = static_cast<float>(y % cell) / static_cast<float>(cell) - 0.5F;
      const float alpha = std::clamp(1.0F - 2.0F * std::sqrt(dx * dx + dy * dy), 0.0F, 1.0F);
      std::uint8_t *px = &image.pixels[(y * size + x) * 4];
      px[0] = 255;
      px[1] = 255;
      px[2] = 255;
      px[3] = static_cast<std::uint8_t>(alpha * 255.0F);
    }
  }
  return image;
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

  const auto checker = require(
      renderer->createTexture(makeChecker(256, 32), Paper::ColorSpace::Srgb));
  const auto dots = require(renderer->createTexture(
      makeDots(256, 64), Paper::ColorSpace::Srgb,
      {.wrapU = Paper::WrapMode::Repeat, .wrapV = Paper::WrapMode::Repeat}));

  // Three materials sharing one shader but differing in parameters and state.
  const auto red = require(renderer->createMaterial(shader));
  require(renderer->setMaterialParam(red, "baseColor", glm::vec4(0.9F, 0.25F, 0.2F, 1.0F)));
  require(renderer->setMaterialParam(red, "normalMix", 0.0F));
  require(renderer->setMaterialTexture(red, "baseColorTexture", checker));

  const auto green = require(renderer->createMaterial(shader));
  require(renderer->setMaterialParam(green, "baseColor", glm::vec4(0.2F, 0.8F, 0.4F, 1.0F)));
  require(renderer->setMaterialParam(green, "normalMix", 0.35F));
  // No texture assigned: the white default is used.

  // Alpha-masked and double-sided: the dots shrink and grow as the cutoff
  // changes over time (see the loop).
  const auto masked = require(renderer->createMaterial(
      shader, {.cullMode = Paper::CullMode::None,
               .alphaMode = Paper::AlphaMode::Mask}));
  require(renderer->setMaterialParam(masked, "baseColor", glm::vec4(1.0F, 0.85F, 0.2F, 0.5F)));
  require(renderer->setMaterialParam(masked, "normalMix", 0.0F));
  require(renderer->setMaterialParam(masked, "alphaCutoff", 0.25F));
  require(renderer->setMaterialTexture(masked, "baseColorTexture", dots));

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
  renderer->destroyTexture(checker);
  renderer->destroyTexture(dots);
  renderer->destroyMesh(cube);
  return 0;
}
