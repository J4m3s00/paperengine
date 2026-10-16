#pragma once

#include "core/renderer/mesh.h"
#include "game/map.h"

#include <string>
#include <vector>

// Render geometry for one material of a map.
struct MapMesh {
  std::string material;
  Paper::MeshData mesh;
};

// Triangulates every brush face of `map`, merged per material name. UVs are a
// world-space planar projection (1 uv unit = kUvUnits world units), so textures
// have a constant size on every surface.
[[nodiscard]] std::vector<MapMesh> buildMapMeshes(const Paper::MapData &map);

inline constexpr float kUvUnits = 64.0F;
