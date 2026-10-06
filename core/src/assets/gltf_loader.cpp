#include "gltf_loader.h"

#include "core/assets/image.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cfloat>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>

namespace Paper {
namespace {

template <typename... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

std::unexpected<Error> fail(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}

struct Aabb {
  glm::vec3 min{FLT_MAX};
  glm::vec3 max{-FLT_MAX};

  void expand(const glm::vec3 &point) {
    min = glm::min(min, point);
    max = glm::max(max, point);
  }
  [[nodiscard]] bool valid() const { return min.x <= max.x; }
};

SamplerSettings toSampler(const fastgltf::Sampler &sampler) {
  const auto filter = [](const fastgltf::Optional<fastgltf::Filter> &f) {
    if (!f) {
      return Filter::Linear;
    }
    switch (*f) {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
      return Filter::Nearest;
    default:
      return Filter::Linear;
    }
  };
  const auto wrap = [](fastgltf::Wrap w) {
    switch (w) {
    case fastgltf::Wrap::ClampToEdge:
      return WrapMode::ClampToEdge;
    case fastgltf::Wrap::MirroredRepeat:
      return WrapMode::MirroredRepeat;
    case fastgltf::Wrap::Repeat:
      return WrapMode::Repeat;
    }
    return WrapMode::Repeat;
  };
  return {filter(sampler.magFilter), filter(sampler.minFilter),
          wrap(sampler.wrapS), wrap(sampler.wrapT)};
}

std::span<const std::uint8_t> asBytes(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()};
}

class Importer {
public:
  Importer(Renderer &renderer, const std::filesystem::path &path,
           ShaderHandle shader)
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
  std::expected<void, Error> build();
  std::expected<std::vector<MeshRenderer>, Error> meshFor(std::size_t meshIndex);
  std::expected<MaterialHandle, Error>
  materialFor(fastgltf::Optional<std::size_t> materialIndex);
  std::expected<TextureHandle, Error> textureFor(std::size_t textureIndex,
                                                 ColorSpace colorSpace);
  std::expected<Image, Error> decodeImageSource(const fastgltf::Image &image);
  [[nodiscard]] std::span<const std::byte>
  bufferBytes(const fastgltf::DataSource &source) const;
  void computeBounds();

  Renderer &m_renderer;
  std::filesystem::path m_path;
  ShaderHandle m_shader;
  fastgltf::Asset m_asset;
  Model m_model;

  std::map<std::size_t, std::vector<MeshRenderer>> m_meshCache;
  std::map<std::size_t, MaterialHandle> m_materialCache;
  std::map<std::pair<std::size_t, ColorSpace>, TextureHandle> m_textureCache;
  std::unordered_map<std::uint32_t, Aabb> m_meshBounds; // By MeshHandle index.
};

std::span<const std::byte>
Importer::bufferBytes(const fastgltf::DataSource &source) const {
  return std::visit(
      Overloaded{
          [](const fastgltf::sources::Array &a) {
            return std::span<const std::byte>(a.bytes.data(), a.bytes.size());
          },
          [](const fastgltf::sources::Vector &v) {
            return std::span<const std::byte>(v.bytes.data(), v.bytes.size());
          },
          [](const fastgltf::sources::ByteView &v) {
            return std::span<const std::byte>(v.bytes.data(), v.bytes.size());
          },
          [](const auto &) { return std::span<const std::byte>(); }},
      source);
}

std::expected<Image, Error>
Importer::decodeImageSource(const fastgltf::Image &image) {
  return std::visit(
      Overloaded{
          [&](const fastgltf::sources::BufferView &view)
              -> std::expected<Image, Error> {
            const fastgltf::BufferView &bufferView =
                m_asset.bufferViews[view.bufferViewIndex];
            const std::span<const std::byte> all =
                bufferBytes(m_asset.buffers[bufferView.bufferIndex].data);
            if (bufferView.byteOffset + bufferView.byteLength > all.size()) {
              return fail(ErrorCode::Asset, "Image buffer view is out of range");
            }
            return decodeImage(asBytes(
                all.subspan(bufferView.byteOffset, bufferView.byteLength)));
          },
          [&](const fastgltf::sources::URI &uri) -> std::expected<Image, Error> {
            if (!uri.uri.isLocalPath()) {
              return fail(ErrorCode::Asset, "Unsupported image URI");
            }
            return loadImage(m_path.parent_path() / uri.uri.fspath());
          },
          [&](const auto &) -> std::expected<Image, Error> {
            const std::span<const std::byte> bytes = bufferBytes(image.data);
            if (bytes.empty()) {
              return fail(ErrorCode::Asset, "Image has no usable data source");
            }
            return decodeImage(asBytes(bytes));
          }},
      image.data);
}

std::expected<TextureHandle, Error>
Importer::textureFor(std::size_t textureIndex, ColorSpace colorSpace) {
  const auto key = std::pair(textureIndex, colorSpace);
  if (const auto it = m_textureCache.find(key); it != m_textureCache.end()) {
    return it->second;
  }
  const fastgltf::Texture &texture = m_asset.textures[textureIndex];
  if (!texture.imageIndex) {
    return fail(ErrorCode::Asset,
                "Texture " + std::to_string(textureIndex) +
                    " has no supported image (only PNG/JPEG)");
  }
  auto image = decodeImageSource(m_asset.images[*texture.imageIndex]);
  if (!image) {
    return std::unexpected(image.error());
  }
  const SamplerSettings sampler =
      texture.samplerIndex ? toSampler(m_asset.samplers[*texture.samplerIndex])
                           : SamplerSettings{};
  auto handle = m_renderer.createTexture(*image, colorSpace, sampler);
  if (!handle) {
    return std::unexpected(handle.error());
  }
  m_model.textures.push_back(*handle);
  m_textureCache.emplace(key, *handle);
  return *handle;
}

std::expected<MaterialHandle, Error>
Importer::materialFor(fastgltf::Optional<std::size_t> materialIndex) {
  // Primitives without a material share one default glTF material.
  const std::size_t key = materialIndex ? *materialIndex : m_asset.materials.size();
  if (const auto it = m_materialCache.find(key); it != m_materialCache.end()) {
    return it->second;
  }
  const fastgltf::Material defaultMaterial{};
  const fastgltf::Material &source =
      materialIndex ? m_asset.materials[*materialIndex] : defaultMaterial;

  RenderState state;
  state.cullMode = source.doubleSided ? CullMode::None : CullMode::Back;
  if (source.alphaMode == fastgltf::AlphaMode::Mask) {
    state.alphaMode = AlphaMode::Mask;
  } else if (source.alphaMode == fastgltf::AlphaMode::Blend) {
    spdlog::warn("glTF material {} uses alpha blending, which is not "
                 "supported yet; rendering it opaque",
                 key);
  }

  auto created = m_renderer.createMaterial(m_shader, state);
  if (!created) {
    return std::unexpected(created.error());
  }
  const MaterialHandle material = *created;
  m_model.materials.push_back(material);
  m_materialCache.emplace(key, material);

  // Parameters the shader does not declare are skipped.
  const auto setParam = [&](std::string_view name,
                            const MaterialParam &value) -> std::expected<void, Error> {
    if (!m_renderer.hasMaterialParam(m_shader, name)) {
      return {};
    }
    return m_renderer.setMaterialParam(material, name, value);
  };
  const auto setTexture =
      [&](std::string_view slot,
          const fastgltf::Optional<fastgltf::TextureInfo> &info,
          ColorSpace colorSpace) -> std::expected<void, Error> {
    if (!info || !m_renderer.hasTextureSlot(m_shader, slot)) {
      return {};
    }
    if (info->texCoordIndex != 0) {
      spdlog::warn("glTF material {}: {} uses TEXCOORD_{}; only TEXCOORD_0 is "
                   "supported",
                   key, slot, info->texCoordIndex);
    }
    auto texture = textureFor(info->textureIndex, colorSpace);
    if (!texture) {
      return std::unexpected(texture.error());
    }
    return m_renderer.setMaterialTexture(material, slot, *texture);
  };

  const auto &pbr = source.pbrData;
  const auto &base = pbr.baseColorFactor;
  const auto &emissive = source.emissiveFactor;
  const float emissiveStrength = static_cast<float>(source.emissiveStrength);

  for (const auto &step : std::initializer_list<std::function<std::expected<void, Error>()>>{
           [&] {
             return setParam("baseColorFactor",
                             glm::vec4(base[0], base[1], base[2], base[3]));
           },
           [&] { return setParam("metallicFactor", static_cast<float>(pbr.metallicFactor)); },
           [&] { return setParam("roughnessFactor", static_cast<float>(pbr.roughnessFactor)); },
           [&] {
             return setParam("emissiveFactor",
                             glm::vec3(emissive[0], emissive[1], emissive[2]) *
                                 emissiveStrength);
           },
           [&] {
             return setParam("normalScale",
                             source.normalTexture
                                 ? static_cast<float>(source.normalTexture->scale)
                                 : 1.0F);
           },
           [&] {
             return setParam("occlusionStrength",
                             source.occlusionTexture
                                 ? static_cast<float>(source.occlusionTexture->strength)
                                 : 1.0F);
           },
           [&] { return setParam("alphaCutoff", static_cast<float>(source.alphaCutoff)); },
           [&] { return setTexture("baseColorTexture", pbr.baseColorTexture, ColorSpace::Srgb); },
           [&] { return setTexture("metallicRoughnessTexture", pbr.metallicRoughnessTexture, ColorSpace::Linear); },
           [&] {
             const fastgltf::Optional<fastgltf::TextureInfo> normal =
                 source.normalTexture
                     ? fastgltf::Optional<fastgltf::TextureInfo>(
                           fastgltf::TextureInfo{source.normalTexture->textureIndex,
                                                 source.normalTexture->texCoordIndex,
                                                 nullptr})
                     : std::nullopt;
             return setTexture("normalTexture", normal, ColorSpace::Linear);
           },
           [&] {
             const fastgltf::Optional<fastgltf::TextureInfo> occlusion =
                 source.occlusionTexture
                     ? fastgltf::Optional<fastgltf::TextureInfo>(
                           fastgltf::TextureInfo{source.occlusionTexture->textureIndex,
                                                 source.occlusionTexture->texCoordIndex,
                                                 nullptr})
                     : std::nullopt;
             return setTexture("occlusionTexture", occlusion, ColorSpace::Linear);
           },
           [&] { return setTexture("emissiveTexture", source.emissiveTexture, ColorSpace::Srgb); },
       }) {
    if (auto r = step(); !r) {
      return std::unexpected(r.error());
    }
  }
  return material;
}

std::expected<std::vector<MeshRenderer>, Error>
Importer::meshFor(std::size_t meshIndex) {
  if (const auto it = m_meshCache.find(meshIndex); it != m_meshCache.end()) {
    return it->second;
  }
  const fastgltf::Mesh &mesh = m_asset.meshes[meshIndex];
  std::vector<MeshRenderer> renderers;

  for (const fastgltf::Primitive &primitive : mesh.primitives) {
    if (primitive.type != fastgltf::PrimitiveType::Triangles) {
      spdlog::warn("glTF mesh {}: skipping a non-triangle-list primitive",
                   meshIndex);
      continue;
    }

    const auto attribute = [&](std::string_view name)
        -> const fastgltf::Accessor * {
      const auto it = primitive.findAttribute(name);
      return it == primitive.attributes.cend()
                 ? nullptr
                 : &m_asset.accessors[it->accessorIndex];
    };

    const fastgltf::Accessor *positions = attribute("POSITION");
    if (positions == nullptr || positions->type != fastgltf::AccessorType::Vec3) {
      return fail(ErrorCode::Asset, "glTF mesh " + std::to_string(meshIndex) +
                                        " has a primitive without POSITION");
    }
    const std::size_t count = positions->count;
    MeshData data;
    data.vertices.assign(count, Vertex{{0, 0, 0}, {0, 0, 0}, {0, 0}, {0, 0, 0, 0}});

    Aabb bounds;
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        m_asset, *positions, [&](const glm::vec3 &v, std::size_t i) {
          data.vertices[i].position = v;
          bounds.expand(v);
        });

    const fastgltf::Accessor *normals = attribute("NORMAL");
    if (normals != nullptr && normals->type == fastgltf::AccessorType::Vec3 &&
        normals->count == count) {
      fastgltf::iterateAccessorWithIndex<glm::vec3>(
          m_asset, *normals, [&](const glm::vec3 &v, std::size_t i) {
            data.vertices[i].normal = v;
          });
    }
    const fastgltf::Accessor *uvs = attribute("TEXCOORD_0");
    if (uvs != nullptr && uvs->type == fastgltf::AccessorType::Vec2 &&
        uvs->count == count) {
      fastgltf::iterateAccessorWithIndex<glm::vec2>(
          m_asset, *uvs, [&](const glm::vec2 &v, std::size_t i) {
            data.vertices[i].uv = v;
          });
    }
    // tangent.w == 0 marks "no tangent": shaders derive one from the UVs.
    const fastgltf::Accessor *tangents = attribute("TANGENT");
    if (tangents != nullptr && tangents->type == fastgltf::AccessorType::Vec4 &&
        tangents->count == count) {
      fastgltf::iterateAccessorWithIndex<glm::vec4>(
          m_asset, *tangents, [&](const glm::vec4 &v, std::size_t i) {
            data.vertices[i].tangent = v;
          });
    }

    if (primitive.indicesAccessor) {
      const fastgltf::Accessor &indices = m_asset.accessors[*primitive.indicesAccessor];
      if (indices.type != fastgltf::AccessorType::Scalar) {
        return fail(ErrorCode::Asset, "glTF index accessor is not scalar");
      }
      data.indices.resize(indices.count);
      fastgltf::copyFromAccessor<std::uint32_t>(m_asset, indices,
                                                data.indices.data());
    } else {
      data.indices.resize(count);
      for (std::size_t i = 0; i < count; ++i) {
        data.indices[i] = static_cast<std::uint32_t>(i);
      }
    }
    if (data.indices.size() % 3 != 0) {
      return fail(ErrorCode::Asset,
                  "glTF primitive index count is not a multiple of 3");
    }

    if (normals == nullptr) {
      // Smooth normals: accumulate area-weighted face normals per vertex.
      for (std::size_t i = 0; i + 2 < data.indices.size(); i += 3) {
        const std::uint32_t a = data.indices[i];
        const std::uint32_t b = data.indices[i + 1];
        const std::uint32_t c = data.indices[i + 2];
        if (a >= count || b >= count || c >= count) {
          break; // createMesh reports the out-of-range index.
        }
        const glm::vec3 face =
            glm::cross(data.vertices[b].position - data.vertices[a].position,
                       data.vertices[c].position - data.vertices[a].position);
        data.vertices[a].normal += face;
        data.vertices[b].normal += face;
        data.vertices[c].normal += face;
      }
      for (Vertex &vertex : data.vertices) {
        const float length = glm::length(vertex.normal);
        vertex.normal = length > 0.0F ? vertex.normal / length
                                      : glm::vec3(0.0F, 1.0F, 0.0F);
      }
    }

    auto meshHandle = m_renderer.createMesh(data);
    if (!meshHandle) {
      return std::unexpected(meshHandle.error());
    }
    m_model.meshes.push_back(*meshHandle);
    m_meshBounds.emplace(meshHandle->index, bounds);

    auto material = materialFor(primitive.materialIndex);
    if (!material) {
      return std::unexpected(material.error());
    }
    renderers.push_back({*meshHandle, *material});
  }

  m_meshCache.emplace(meshIndex, renderers);
  return renderers;
}

void Importer::computeBounds() {
  Aabb total;
  const auto visit = [&](const auto &self, std::uint32_t nodeIndex,
                         const glm::mat4 &parent) -> void {
    const ModelNode &node = m_model.nodes[nodeIndex];
    const glm::mat4 world = parent * node.transform.matrix();
    for (const MeshRenderer &primitive : node.primitives) {
      const auto it = m_meshBounds.find(primitive.mesh.index);
      if (it == m_meshBounds.end() || !it->second.valid()) {
        continue;
      }
      const Aabb &box = it->second;
      for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) != 0 ? box.max.x : box.min.x,
                              (corner & 2) != 0 ? box.max.y : box.min.y,
                              (corner & 4) != 0 ? box.max.z : box.min.z);
        total.expand(glm::vec3(world * glm::vec4(point, 1.0F)));
      }
    }
    for (const std::uint32_t child : node.children) {
      self(self, child, world);
    }
  };
  for (const std::uint32_t root : m_model.rootNodes) {
    visit(visit, root, glm::mat4(1.0F));
  }
  if (total.valid()) {
    m_model.boundsMin = total.min;
    m_model.boundsMax = total.max;
  }
}

std::expected<void, Error> Importer::build() {
  auto data = fastgltf::GltfDataBuffer::FromPath(m_path);
  if (data.error() != fastgltf::Error::None) {
    return fail(ErrorCode::Io, "Cannot read glTF file " + m_path.string() +
                                   ": " +
                                   std::string(fastgltf::getErrorMessage(data.error())));
  }

  fastgltf::Parser parser;
  auto loaded = parser.loadGltf(
      data.get(), m_path.parent_path(),
      fastgltf::Options::LoadExternalBuffers |
          fastgltf::Options::LoadExternalImages |
          fastgltf::Options::DecomposeNodeMatrices);
  if (loaded.error() != fastgltf::Error::None) {
    return fail(ErrorCode::Asset, "Failed to parse " + m_path.string() + ": " +
                                      std::string(fastgltf::getErrorMessage(loaded.error())));
  }
  m_asset = std::move(loaded.get());

  m_model.nodes.resize(m_asset.nodes.size());
  for (std::size_t i = 0; i < m_asset.nodes.size(); ++i) {
    const fastgltf::Node &source = m_asset.nodes[i];
    ModelNode &node = m_model.nodes[i];
    node.name = std::string(source.name);

    if (const auto *trs = std::get_if<fastgltf::TRS>(&source.transform)) {
      node.transform.position = {trs->translation[0], trs->translation[1],
                                 trs->translation[2]};
      node.transform.rotation = glm::quat(trs->rotation[3], trs->rotation[0],
                                          trs->rotation[1], trs->rotation[2]);
      node.transform.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
    }
    for (const std::size_t child : source.children) {
      node.children.push_back(static_cast<std::uint32_t>(child));
    }
    if (source.meshIndex) {
      auto primitives = meshFor(*source.meshIndex);
      if (!primitives) {
        return std::unexpected(primitives.error());
      }
      node.primitives = std::move(*primitives);
    }
  }

  if (!m_asset.scenes.empty()) {
    const std::size_t sceneIndex = m_asset.defaultScene.value_or(0);
    if (sceneIndex >= m_asset.scenes.size()) {
      return fail(ErrorCode::Asset, "glTF default scene does not exist");
    }
    for (const std::size_t node : m_asset.scenes[sceneIndex].nodeIndices) {
      m_model.rootNodes.push_back(static_cast<std::uint32_t>(node));
    }
  } else {
    std::vector<bool> isChild(m_model.nodes.size(), false);
    for (const ModelNode &node : m_model.nodes) {
      for (const std::uint32_t child : node.children) {
        isChild[child] = true;
      }
    }
    for (std::uint32_t i = 0; i < m_model.nodes.size(); ++i) {
      if (!isChild[i]) {
        m_model.rootNodes.push_back(i);
      }
    }
  }

  computeBounds();
  return {};
}
} // namespace

std::expected<Model, Error> importGltf(Renderer &renderer,
                                       const std::filesystem::path &path,
                                       ShaderHandle shader) {
  return Importer(renderer, path, shader).run();
}

void destroyModelResources(Renderer &renderer, const Model &model) {
  // Materials first: they hold references to the textures.
  for (const MaterialHandle material : model.materials) {
    renderer.destroyMaterial(material);
  }
  for (const TextureHandle texture : model.textures) {
    renderer.destroyTexture(texture);
  }
  for (const MeshHandle mesh : model.meshes) {
    renderer.destroyMesh(mesh);
  }
}
} // namespace Paper
