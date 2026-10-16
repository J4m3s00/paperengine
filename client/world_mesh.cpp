#include "world_mesh.h"

#include "game/brush_geometry.h"

#include <cmath>
#include <map>

std::vector<MapMesh> buildMapMeshes(const Paper::MapData &map) {
  std::map<std::string, std::size_t> byMaterial;
  std::vector<MapMesh> meshes;

  for (const Paper::MapBrush &brush : map.brushes) {
    auto [it, inserted] = byMaterial.try_emplace(brush.material, meshes.size());
    if (inserted) {
      meshes.push_back({brush.material, {}});
    }
    Paper::MeshData &mesh = meshes[it->second].mesh;

    for (const Paper::BrushFace &face : Paper::brushFaces(brush.brush)) {
      const glm::vec3 &n = face.plane.normal;
      const glm::vec3 absN = glm::abs(n);
      // Project along the dominant axis.
      glm::vec3 uAxis(1, 0, 0);
      glm::vec3 vAxis(0, 0, 1);
      if (absN.x >= absN.y && absN.x >= absN.z) {
        uAxis = {0, 0, 1};
        vAxis = {0, 1, 0};
      } else if (absN.z >= absN.y) {
        uAxis = {1, 0, 0};
        vAxis = {0, 1, 0};
      }
      const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
      for (const glm::vec3 &p : face.vertices) {
        mesh.vertices.push_back({p, n,
                                 {glm::dot(p, uAxis) / kUvUnits, glm::dot(p, vAxis) / kUvUnits},
                                 glm::vec4(uAxis, 1.0F)});
      }
      for (std::uint32_t i = 1; i + 1 < face.vertices.size(); ++i) {
        mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
      }
    }
  }
  return meshes;
}
