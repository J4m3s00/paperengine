# 0005: GLSL compiled with `glslc`, material parameters from reflection

Status: accepted

## Decision

- Shaders are GLSL in `core/shaders/`. CMake compiles them with `glslc` into `<build>/shaders/*.spv` as a dependency of the `core` target. They are loaded at runtime.
- The `assetRoot` is configurable on the Renderer. By default it is a path relative to the executable; in dev builds a CMake define points it at the build directory.
- A Material is Shader + parameters + textures + render state. Parameters are set by name (`material.set("baseColorFactor", vec4)`) and validated against the SPIR-V reflection; unknown names or wrong types return an `Error`.
- Render state (cull mode, alpha mode, depth test) belongs to the Material. Pipelines are cached by (shader, render state, vertex layout, attachment formats).
- Alpha modes: `OPAQUE` and `MASK` (discard). `BLEND` is deferred.

## Consequences

- No runtime shader compiler dependency; hot reload can be added later without API changes.
- Custom shaders are possible within the binding conventions of ADR 0004.
