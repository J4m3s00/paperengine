#include "fbx_loader.h"

#include "gltf_loader.h"

#include <ufbx.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cfloat>
#include <map>
#include <string>

namespace Paper {
namespace {
std::unexpected<Error> fail(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}

glm::vec3 toVec3(const ufbx_vec3 &v) {
  return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

class SceneGuard {
public:
  explicit SceneGuard(ufbx_scene *scene) : m_scene(scene) {}
  ~SceneGuard() { ufbx_free_scene(m_scene); }
  SceneGuard(const SceneGuard &) = delete;
  SceneGuard &operator=(const SceneGuard &) = delete;
  [[nodiscard]] ufbx_scene *get() const { return m_scene; }

private:
  ufbx_scene *m_scene;
};

class Importer {
public:
  Importer(Renderer &renderer, const std::filesystem::path &path, ShaderHandle shader)
      : m_renderer(renderer), m_path(path), m_shader(shader) {}

  std::expected<Model, Error> run() {
    auto built = build();
    if (!built) {
      destroyModelResources(m_renderer, m_model);
      return std::unexpected(built.error());
    }
    return std::move(m_model);
  }

private:
  // The texture a material names, looked for next to the file and one folder up
  // (the prototype kit keeps one shared texture beside its piece folders).
  std::optional<TextureHandle> textureFor(const ufbx_texture *texture) {
    if (texture == nullptr) {
      return std::nullopt;
    }
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path dir = m_path.parent_path();
    const std::filesystem::path relative(std::string(texture->relative_filename.data, texture->relative_filename.length));
    const std::filesystem::path name = std::filesystem::path(std::string(texture->filename.data, texture->filename.length)).filename();
    for (const auto &base : {dir, dir.parent_path()}) {
      candidates.push_back(base / relative);
      candidates.push_back(base / relative.filename());
      candidates.push_back(base / name);
    }
    for (const auto &candidate : candidates) {
      std::error_code ec;
      if (candidate.empty() || !std::filesystem::exists(candidate, ec)) {
        continue;
      }
      if (const auto cached = m_textures.find(candidate.string()); cached != m_textures.end()) {
        return cached->second;
      }
      // Nearest filtering: the kit's texture is a tiny palette image.
      auto handle = m_renderer.loadTexture(candidate, ColorSpace::Srgb,
                                           {.magFilter = Filter::Nearest,
                                            .minFilter = Filter::Nearest});
      if (!handle) {
        spdlog::warn("fbx texture {}: {}", candidate.string(), handle.error().message);
        return std::nullopt;
      }
      m_model.textures.push_back(*handle);
      m_textures[candidate.string()] = *handle;
      return *handle;
    }
    return std::nullopt;
  }

  std::expected<MaterialHandle, Error> materialFor(const ufbx_material *source) {
    const void *key = source;
    if (const auto it = m_materials.find(key); it != m_materials.end()) {
      return it->second;
    }
    auto material = m_renderer.createMaterial(m_shader, {});
    if (!material) {
      return std::unexpected(material.error());
    }
    m_model.materials.push_back(*material);
    glm::vec4 color(0.8F, 0.8F, 0.8F, 1.0F);
    std::optional<TextureHandle> texture;
    if (source != nullptr) {
      const ufbx_material_map &base = source->pbr.base_color;
      if (base.has_value) {
        color = {static_cast<float>(base.value_vec4.x), static_cast<float>(base.value_vec4.y),
                 static_cast<float>(base.value_vec4.z), 1.0F};
      }
      texture = textureFor(base.texture);
    }
    // A textured material's color is the texture's; the factor must not tint it.
    if (texture) {
      color = glm::vec4(1.0F);
    }
    (void)m_renderer.setMaterialParam(*material, "baseColorFactor", color);
    (void)m_renderer.setMaterialParam(*material, "metallicFactor", 0.0F);
    (void)m_renderer.setMaterialParam(*material, "roughnessFactor", 0.9F);
    (void)m_renderer.setMaterialParam(*material, "emissiveFactor", glm::vec3(0.0F));
    (void)m_renderer.setMaterialParam(*material, "normalScale", 1.0F);
    (void)m_renderer.setMaterialParam(*material, "occlusionStrength", 1.0F);
    (void)m_renderer.setMaterialParam(*material, "alphaCutoff", 0.5F);
    if (texture) {
      (void)m_renderer.setMaterialTexture(*material, "baseColorTexture", *texture);
    }
    m_materials[key] = *material;
    return *material;
  }

  std::expected<std::vector<MeshRenderer>, Error> meshFor(const ufbx_node &node) {
    const ufbx_mesh &mesh = *node.mesh;
    std::vector<MeshRenderer> renderers;
    std::vector<std::uint32_t> triangle(std::max<std::size_t>(mesh.max_face_triangles * 3, 3));
    for (std::size_t part = 0; part < mesh.material_parts.count; ++part) {
      const ufbx_mesh_part &p = mesh.material_parts.data[part];
      if (p.num_triangles == 0) {
        continue;
      }
      MeshData data;
      data.vertices.reserve(p.num_triangles * 3);
      std::pair<glm::vec3, glm::vec3> local{glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX)};
      for (std::size_t f = 0; f < p.face_indices.count; ++f) {
        const ufbx_face face = mesh.faces.data[p.face_indices.data[f]];
        const std::uint32_t triangles = ufbx_triangulate_face(triangle.data(), triangle.size(), &mesh, face);
        for (std::uint32_t i = 0; i < triangles * 3; ++i) {
          const std::uint32_t corner = triangle[i];
          Vertex v{};
          v.position = toVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, corner));
          v.normal = mesh.vertex_normal.exists ? toVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, corner))
                                               : glm::vec3(0.0F, 1.0F, 0.0F);
          if (mesh.vertex_uv.exists) {
            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, corner);
            v.uv = {static_cast<float>(uv.x), 1.0F - static_cast<float>(uv.y)}; // FBX v points up
          }
          data.vertices.push_back(v);
          data.indices.push_back(static_cast<std::uint32_t>(data.indices.size()));
          local.first = glm::min(local.first, v.position);
          local.second = glm::max(local.second, v.position);
        }
      }
      // FBX is right handed like glTF but triangulation keeps the file's
      // winding; ufbx's right-handed target makes it counter-clockwise.
      auto handle = m_renderer.createMesh(data);
      if (!handle) {
        return std::unexpected(handle.error());
      }
      m_model.meshes.push_back(*handle);
      m_meshBounds[handle->index] = local;
      const ufbx_material *source =
          part < node.materials.count ? node.materials.data[part]
                                      : (part < mesh.materials.count ? mesh.materials.data[part] : nullptr);
      auto material = materialFor(source);
      if (!material) {
        return std::unexpected(material.error());
      }
      renderers.push_back({*handle, *material});
    }
    return renderers;
  }

  std::expected<void, Error> build() {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0F;
    // Bake the unit/axis conversion into the nodes and meshes: the file's root
    // node is not part of the Model, so it cannot carry it.
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;
    ufbx_error error;
    const std::string file = m_path.string();
    SceneGuard scene(ufbx_load_file(file.c_str(), &opts, &error));
    if (scene.get() == nullptr) {
      return fail(ErrorCode::Asset, "Cannot load FBX " + file + ": " + std::string(error.description.data, error.description.length));
    }
    m_bounds = {glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX)};
    const ufbx_scene &s = *scene.get();
    m_model.nodes.resize(s.nodes.count);
    for (std::size_t i = 0; i < s.nodes.count; ++i) {
      const ufbx_node &source = *s.nodes.data[i];
      ModelNode &node = m_model.nodes[i];
      node.name = std::string(source.name.data, source.name.length);
      const ufbx_transform &t = source.local_transform;
      node.transform.position = toVec3(t.translation);
      node.transform.rotation = glm::quat(static_cast<float>(t.rotation.w), static_cast<float>(t.rotation.x),
                                          static_cast<float>(t.rotation.y), static_cast<float>(t.rotation.z));
      node.transform.scale = toVec3(t.scale);
      for (std::size_t c = 0; c < source.children.count; ++c) {
        node.children.push_back(static_cast<std::uint32_t>(source.children.data[c]->typed_id));
      }
      if (source.parent == nullptr || source.parent->is_root) {
        if (!source.is_root) {
          m_model.rootNodes.push_back(static_cast<std::uint32_t>(i));
        }
      }
      if (source.mesh != nullptr) {
        auto primitives = meshFor(source);
        if (!primitives) {
          return std::unexpected(primitives.error());
        }
        node.primitives = std::move(*primitives);
      }
    }
    // Bounds of everything in model space (node transforms applied).
    const auto visit = [&](const auto &self, std::uint32_t index, const glm::mat4 &parent) -> void {
      const ModelNode &node = m_model.nodes[index];
      const glm::mat4 world = parent * node.transform.matrix();
      for (const MeshRenderer &primitive : node.primitives) {
        const auto it = m_meshBounds.find(primitive.mesh.index);
        if (it == m_meshBounds.end()) continue;
        for (int corner = 0; corner < 8; ++corner) {
          const glm::vec3 point((corner & 1) != 0 ? it->second.second.x : it->second.first.x,
                                (corner & 2) != 0 ? it->second.second.y : it->second.first.y,
                                (corner & 4) != 0 ? it->second.second.z : it->second.first.z);
          const glm::vec3 w = glm::vec3(world * glm::vec4(point, 1.0F));
          m_bounds.first = glm::min(m_bounds.first, w);
          m_bounds.second = glm::max(m_bounds.second, w);
        }
      }
      for (const std::uint32_t child : node.children) self(self, child, world);
    };
    for (const std::uint32_t root : m_model.rootNodes) visit(visit, root, glm::mat4(1.0F));
    if (m_bounds.first.x <= m_bounds.second.x) {
      m_model.boundsMin = m_bounds.first;
      m_model.boundsMax = m_bounds.second;
    }
    return {};
  }

  Renderer &m_renderer;
  std::filesystem::path m_path;
  ShaderHandle m_shader;
  Model m_model;
  std::pair<glm::vec3, glm::vec3> m_bounds;
  std::map<const void *, MaterialHandle> m_materials;
  std::map<std::uint32_t, std::pair<glm::vec3, glm::vec3>> m_meshBounds; // By mesh handle index.
  std::map<std::string, TextureHandle> m_textures;
};
} // namespace

std::expected<Model, Error> importFbx(Renderer &renderer, const std::filesystem::path &path,
                                      ShaderHandle shader) {
  return Importer(renderer, path, shader).run();
}
} // namespace Paper
