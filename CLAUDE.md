# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Paper Engine: an early-stage C++23 Vulkan 1.3 engine (CMake project `myengine`). Two targets: `core` (static library, namespace `Paper`) and `sandbox` (executable that drives `core`). Private GitHub repo (`J4m3s00/paperengine`); no tests or linter configured yet.

## Commands

Requires CMake ≥ 3.28, Ninja, ccache (used as compiler launcher in the presets), and the Vulkan SDK (`find_package(Vulkan COMPONENTS glslc)`). All other dependencies are fetched via FetchContent on first configure.

```
cmake --preset debug          # configure → build/debug (also: release → build/release)
cmake --build build/debug     # build
./build/debug/sandbox/sandbox # run
```

VS Code tasks ("build debug") and the LLDB launch config "Debug sandbox" wrap these. `.clangd` points at `build/debug/compile_commands.json`, so configure the debug preset before relying on clangd.

## Architecture

- `core/include/core/*.h` is the public header root (included as `"core/xxx.h"`); implementations are in `core/src/`. New `.cpp` files must be added to the `add_library(core ...)` list in `core/CMakeLists.txt` (no globbing).
- Dependency visibility matters: `glfw`, `glm`, `spdlog`, `Vulkan` are PUBLIC; `vk-bootstrap` and VMA are PRIVATE. Headers must therefore not include `VkBootstrap.h` or `vk_mem_alloc.h`; `vulkan_context.h` forward-declares `VmaAllocator`. `GLFW_INCLUDE_NONE` is defined for core.
- VMA is compiled once in `src/vma_impl.cpp` (`VMA_IMPLEMENTATION`); other TUs include `vk_mem_alloc.h` with `VMA_VULKAN_VERSION 1003000` defined the same way. Its include dirs are added as SYSTEM to silence third-party warnings.
- Errors are `std::expected<T, Paper::Error>` (`core/error.h`, see `docs/adr/0001-expected-for-error-handling.md`). Fallible types (`Window`, `VulkanContext`, `Swapchain`) have private constructors and a static `create(...)` returning `std::expected<std::unique_ptr<T>, Error>`; they stay non-copyable/non-movable. vk-bootstrap results are translated by `unwrap` in `core/src/vkb_error.h`.
- `Window` uses pimpl (GLFW details hidden in `Window::Impl`), exposes `nativeHandle()` for surface creation.
- `VulkanContext` owns instance, debug messenger, surface, physical/logical device, queues and VMA allocator, built via vk-bootstrap. Requires Vulkan 1.3 with `dynamicRendering` and `synchronization2`. Validation layers default on unless `NDEBUG`. Non-copyable/non-movable; must outlive everything created from it.
- `Swapchain` borrows `const VulkanContext&` and builds images/views; `recreate(window)` returns `expected<bool, Error>`: `false` (keeping the old swapchain) while the framebuffer is 0×0. The sandbox loop currently triggers `recreate` by comparing framebuffer size to swapchain extent.
- `Renderer` (`core/renderer/renderer.h`, pimpl) is the public entry point: `Renderer::create(window)` builds `VulkanContext` and `Swapchain` itself and `render()` draws one frame (2 frames in flight, dynamic rendering, per-swapchain-image render-finished semaphores, resize handled internally). `sandbox` only holds `Window` and `Renderer`; the `Window` must outlive the `Renderer`.
- Shaders: GLSL in `core/shaders/`, listed in `PAPER_SHADER_SOURCES` in `core/CMakeLists.txt` and compiled by `glslc` to `<build>/shaders/<name>.spv` (target `core_shaders`, a dependency of `core`). `Shader::load` reads a `.spv`; `RendererConfig::assetRoot` locates the `shaders/` folder (default: `<exe dir>` if it has `shaders/`, else the build dir via `PAPER_DEFAULT_ASSET_ROOT`). New shaders must be added to that list. `mesh.frag` shows the conventions (set 1 `Material` block, `ALPHA_MASK` constant).
- Rendering: `Renderer::render(scene, camera)` draws every entity with `WorldTransform` + `MeshRenderer {mesh, material}`, sorted by (pipeline, material). Set 0 = per-frame UBO (`FrameUniforms`, one per frame in flight), set 1 = per-material UBO, per-object `PushConstants` (model + normal matrix, 128 bytes), fixed `Vertex` layout (`core/renderer/mesh.h`). Depth buffer is `D32_SFLOAT`, shared by all frames in flight (a barrier orders it). Viewport has negative height (Y up), front face counter-clockwise, depth 0..1 (`glm::perspectiveRH_ZO`).
- Materials (ADR 0003-0005): `createShader({vert.spv, frag.spv})` loads and reflects the SPIR-V (SPIRV-Reflect, PRIVATE; `shader_reflection.cpp`) and validates the engine conventions: vertex inputs must match locations 0..3, set 0 is reserved, set 1 may only hold a uniform block at binding 0 (textures arrive with M6). `createMaterial(shader, RenderState)` allocates one UBO + descriptor set per frame in flight; `setMaterialParam(material, "name", value)` is validated against the reflected block (unknown name / wrong type returns an `Error`) and marks the UBO copies dirty. `RenderState` = cull mode, alpha mode (OPAQUE/MASK; MASK is passed as specialization constant 0 `ALPHA_MASK`), depth test. Pipelines are created lazily and cached by (shader, render state, color/depth format) in `Renderer::Impl::getPipeline`.
- Resources: meshes, shaders and materials are `Handle<Tag>` (index + generation, `ResourcePool` in `core/src/renderer/resource_pool.h`) owned by the Renderer. `destroy*` does not free immediately: the object goes into a deletion queue and is released once the frames that may still use it have finished (`retire`/`collectGarbage` in `renderer.cpp`). `destroyShader` requires that no live material uses it. Uploads are synchronous (staging buffer, graphics queue). Internal GPU wrappers (`GpuBuffer`, `GpuImage`, `uploadBuffer`) are in `core/src/renderer/gpu_resources.h`.
- Scene: `core/scene/` — `Scene` wraps `entt::registry` (EnTT is a PUBLIC dependency); components in `components.h` (`Transform`, `Relationship`, `WorldTransform`, `MeshRenderer`); use `Scene::setParent` for the hierarchy and call `TransformSystem::update(scene)` before `render`. `Camera` is a standalone struct passed to `render`.
- Input: `Window` exposes polled state refreshed by `update()` — `keyDown`, `mouseDown`, `mousePosition`, `mouseDelta`, `scrollDelta`, `setCursorMode`, `requestClose` (enums in `core/input.h`, mapped to GLFW codes in `window.cpp`; add keys there as needed). No event system. The fly camera controller lives in `sandbox/fly_camera.h`, not in `core`.
- Private headers live in `core/src` (on the private include path): `vkb_error.h`, `vk_check.h`.
- `core/src/engine.cpp` and `core/include/core/engine.h` are placeholders (essentially empty).

## Conventions

Compiler flags `-Wall -Wextra -Wpedantic` apply to `core` only. Code style: 2-space indent, `m_` member prefix, `[[nodiscard]]` getters, deleted copy/move on RAII Vulkan wrappers (clang-format LLVM-like style).

## Agent skills

### Issue tracker

Issues live in GitHub Issues (`J4m3s00/paperengine`, via the `gh` CLI). See `docs/agents/issue-tracker.md`.

### Domain docs

Single-context: one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
