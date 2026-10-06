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
- `Window` uses pimpl (GLFW details hidden in `Window::Impl`), exposes `nativeHandle()` for surface creation.
- `VulkanContext` owns instance, debug messenger, surface, physical/logical device, queues and VMA allocator, built via vk-bootstrap (errors are thrown as `std::runtime_error` through the `unwrap` helper). Requires Vulkan 1.3 with `dynamicRendering` and `synchronization2`. Validation layers default on unless `NDEBUG`. Non-copyable/non-movable; must outlive everything created from it.
- `Swapchain` borrows `const VulkanContext&` and builds images/views; `recreate(window)` returns false (keeping the old swapchain) while the framebuffer is 0×0. The sandbox loop currently triggers `recreate` by comparing framebuffer size to swapchain extent.
- Destruction order is by reverse declaration in `main` (Window → VulkanContext → Swapchain), so construct in that order.
- `core/src/engine.cpp` and `core/include/core/engine.h` are placeholders (essentially empty).

## Conventions

Compiler flags `-Wall -Wextra -Wpedantic` apply to `core` only. Code style: 2-space indent, `m_` member prefix, `[[nodiscard]]` getters, deleted copy/move on RAII Vulkan wrappers (clang-format LLVM-like style).

## Agent skills

### Issue tracker

Issues live in GitHub Issues (`J4m3s00/paperengine`, via the `gh` CLI). See `docs/agents/issue-tracker.md`.

### Domain docs

Single-context: one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
