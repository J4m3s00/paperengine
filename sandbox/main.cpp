#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "core/scene/transform_system.h"
#include "core/window.h"

#include "fly_camera.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
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

// Usage: sandbox [model.gltf|model.glb] [--screenshot out.png]
// Without a model it shows the demo cubes. With --screenshot it renders a few
// frames, saves a PNG of the last one and exits.
int main(int argc, char **argv) {
  const char *modelPath = nullptr;
  const char *screenshotPath = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else {
      modelPath = argv[i];
    }
  }

  Paper::WindowConfig config{
      .width = 800, .height = 600, .title = "Paper Engine Window"};

  auto window = require(Paper::Window::create(config));
  auto renderer = require(Paper::Renderer::create(*window));

  const auto cube = require(renderer->createMesh(Paper::makeCube()));
  const auto shader = require(
      renderer->createShader({"pbr.vert.spv", "pbr.frag.spv"}));

  const auto checker = require(
      renderer->createTexture(makeChecker(256, 32), Paper::ColorSpace::Srgb));
  const auto dots = require(renderer->createTexture(
      makeDots(256, 64), Paper::ColorSpace::Srgb,
      {.wrapU = Paper::WrapMode::Repeat, .wrapV = Paper::WrapMode::Repeat}));

  // A material starts with all parameters zero, so set the ones the PBR shader
  // reads (the glTF importer does the same for imported materials).
  const auto makeMaterial = [&](const glm::vec4 &baseColor, float metallic,
                                float roughness,
                                const Paper::RenderState &state = {}) {
    const auto material = require(renderer->createMaterial(shader, state));
    require(renderer->setMaterialParam(material, "baseColorFactor", baseColor));
    require(renderer->setMaterialParam(material, "metallicFactor", metallic));
    require(renderer->setMaterialParam(material, "roughnessFactor", roughness));
    require(renderer->setMaterialParam(material, "emissiveFactor", glm::vec3(0.0F)));
    require(renderer->setMaterialParam(material, "normalScale", 1.0F));
    require(renderer->setMaterialParam(material, "occlusionStrength", 1.0F));
    require(renderer->setMaterialParam(material, "alphaCutoff", 0.5F));
    return material;
  };

  // Three materials sharing one shader but differing in parameters and state.
  const auto red = makeMaterial({0.9F, 0.25F, 0.2F, 1.0F}, 0.0F, 0.5F);
  require(renderer->setMaterialTexture(red, "baseColorTexture", checker));

  // A smooth metal; no textures assigned, so the defaults are used.
  const auto green = makeMaterial({0.2F, 0.8F, 0.4F, 1.0F}, 1.0F, 0.25F);

  // Alpha-masked and double-sided: the dots shrink and grow as the cutoff
  // changes over time (see the loop).
  const auto masked =
      makeMaterial({1.0F, 0.85F, 0.2F, 1.0F}, 0.0F, 0.6F,
                   {.cullMode = Paper::CullMode::None,
                    .alphaMode = Paper::AlphaMode::Mask});
  require(renderer->setMaterialTexture(masked, "baseColorTexture", dots));

  Paper::Scene scene;
  entt::entity parent = entt::null;
  entt::entity child = entt::null;
  std::optional<Paper::ModelHandle> model;
  FlyCamera flyCamera({3.0F, 2.5F, 5.0F}, {0.0F, 0.0F, 0.0F});

  // One directional light and a dim constant ambient.
  scene.ambientLight = glm::vec3(0.04F, 0.045F, 0.055F);
  const auto sun = scene.createEntity();
  scene.registry().emplace<Paper::DirectionalLight>(sun);
  scene.registry().get<Paper::Transform>(sun).rotation =
      Paper::lookRotation(glm::vec3(-0.4F, -0.8F, -0.5F));

  if (modelPath != nullptr) {
    model = require(renderer->loadModel(modelPath, shader));
    const Paper::Model &loaded = renderer->model(*model);
    scene.instantiate(loaded);

    // Frame the model: look at its center from a distance that fits it.
    const glm::vec3 center = (loaded.boundsMin + loaded.boundsMax) * 0.5F;
    const float radius = std::max(glm::length(loaded.boundsMax - loaded.boundsMin) * 0.5F, 0.01F);
    const float distance = radius / std::sin(glm::radians(60.0F) * 0.5F) * 1.1F;
    flyCamera = FlyCamera(center + glm::normalize(glm::vec3(1.0F, 0.6F, 1.0F)) * distance, center, radius);
    flyCamera.camera().nearPlane = distance * 0.01F;
    flyCamera.camera().farPlane = distance * 20.0F;
    spdlog::info("Loaded {} ({} nodes, {} meshes, {} materials, {} textures)", modelPath,
                 loaded.nodes.size(), loaded.meshes.size(), loaded.materials.size(),
                 loaded.textures.size());
  } else {
    // A spinning parent cube with an orbiting child that has its own child.
    parent = scene.createEntity();
    scene.registry().emplace<Paper::MeshRenderer>(parent, cube, red);

    child = scene.createEntity();
    scene.registry().emplace<Paper::MeshRenderer>(child, cube, green);
    scene.registry().get<Paper::Transform>(child).position = {2.0F, 0.0F, 0.0F};
    scene.registry().get<Paper::Transform>(child).scale = glm::vec3(0.5F);
    scene.setParent(child, parent);

    const auto grandchild = scene.createEntity();
    scene.registry().emplace<Paper::MeshRenderer>(grandchild, cube, masked);
    scene.registry().get<Paper::Transform>(grandchild).position = {0.0F, 1.5F, 0.0F};
    scene.registry().get<Paper::Transform>(grandchild).scale = glm::vec3(0.5F);
    scene.setParent(grandchild, child);
  }

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

    const double now = window->time();
    flyCamera.update(*window, static_cast<float>(now - lastTime));
    lastTime = now;

    const auto t = static_cast<float>(now);
    if (parent != entt::null) {
      scene.registry().get<Paper::Transform>(parent).rotation =
          glm::angleAxis(t * 0.6F, glm::vec3(0.0F, 1.0F, 0.0F));
      scene.registry().get<Paper::Transform>(child).rotation =
          glm::angleAxis(t * 1.5F, glm::vec3(1.0F, 0.0F, 0.0F));

      // Parameters can change at runtime; each frame in flight gets its own
      // copy.
      require(renderer->setMaterialParam(red, "roughnessFactor", 0.5F + 0.4F * std::sin(t)));
      require(renderer->setMaterialParam(masked, "alphaCutoff", 0.45F + 0.3F * std::sin(t * 2.0F)));
    }
    Paper::TransformSystem::update(scene);

    require(renderer->render(scene, flyCamera.camera()));
  }

  if (model) {
    renderer->destroyModel(*model);
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
