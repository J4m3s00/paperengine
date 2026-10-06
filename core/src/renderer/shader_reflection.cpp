#include "shader_reflection.h"

#include <spirv_reflect.h>

#include <algorithm>

namespace Paper {
namespace {

constexpr ErrorCode kCode = ErrorCode::Shader;

// Fixed vertex layout, see core/renderer/mesh.h.
struct ExpectedInput {
  std::uint32_t location;
  SpvReflectFormat format;
};
constexpr ExpectedInput kVertexInputs[] = {
    {0, SPV_REFLECT_FORMAT_R32G32B32_SFLOAT},
    {1, SPV_REFLECT_FORMAT_R32G32B32_SFLOAT},
    {2, SPV_REFLECT_FORMAT_R32G32_SFLOAT},
    {3, SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT},
};

class ReflectModule {
public:
  ReflectModule() = default;
  ~ReflectModule() {
    if (m_created) {
      spvReflectDestroyShaderModule(&m_module);
    }
  }
  ReflectModule(const ReflectModule &) = delete;
  ReflectModule &operator=(const ReflectModule &) = delete;

  bool create(std::span<const std::uint32_t> spirv) {
    m_created = spvReflectCreateShaderModule(spirv.size_bytes(), spirv.data(),
                                             &m_module) ==
                SPV_REFLECT_RESULT_SUCCESS;
    return m_created;
  }
  SpvReflectShaderModule &get() { return m_module; }

private:
  SpvReflectShaderModule m_module{};
  bool m_created = false;
};

ParamType decodeType(const SpvReflectBlockVariable &member) {
  const SpvReflectTypeDescription *type = member.type_description;
  if (type == nullptr || member.array.dims_count > 0 ||
      (type->type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT) != 0) {
    return ParamType::Unsupported;
  }
  const auto flags = type->type_flags;
  const auto &numeric = member.numeric;
  if (numeric.scalar.width != 32) {
    return ParamType::Unsupported;
  }
  if ((flags & SPV_REFLECT_TYPE_FLAG_MATRIX) != 0) {
    return (flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0 &&
                   numeric.matrix.column_count == 4 &&
                   numeric.matrix.row_count == 4
               ? ParamType::Mat4
               : ParamType::Unsupported;
  }
  if ((flags & SPV_REFLECT_TYPE_FLAG_VECTOR) != 0) {
    if ((flags & SPV_REFLECT_TYPE_FLAG_FLOAT) == 0) {
      return ParamType::Unsupported;
    }
    switch (numeric.vector.component_count) {
    case 2:
      return ParamType::Vec2;
    case 3:
      return ParamType::Vec3;
    case 4:
      return ParamType::Vec4;
    default:
      return ParamType::Unsupported;
    }
  }
  if ((flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0) {
    return ParamType::Float;
  }
  if ((flags & SPV_REFLECT_TYPE_FLAG_INT) != 0) {
    return ParamType::Int;
  }
  return ParamType::Unsupported;
}

std::unexpected<Error> fail(std::string message) {
  return std::unexpected(Error{kCode, std::move(message)});
}

std::expected<void, Error> checkVertexInputs(SpvReflectShaderModule &module) {
  std::uint32_t count = 0;
  spvReflectEnumerateInputVariables(&module, &count, nullptr);
  std::vector<SpvReflectInterfaceVariable *> inputs(count);
  spvReflectEnumerateInputVariables(&module, &count, inputs.data());
  for (const SpvReflectInterfaceVariable *input : inputs) {
    if ((input->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) != 0) {
      continue;
    }
    bool matched = false;
    for (const ExpectedInput &expected : kVertexInputs) {
      if (expected.location == input->location) {
        if (expected.format != input->format) {
          return fail("Vertex input at location " +
                      std::to_string(input->location) +
                      " does not match the engine vertex layout");
        }
        matched = true;
      }
    }
    if (!matched) {
      return fail("Vertex input at location " +
                  std::to_string(input->location) +
                  " is not part of the engine vertex layout (0..3)");
    }
  }
  return {};
}

// Returns the set 1 layout used by this stage (empty if it uses none).
std::expected<MaterialLayout, Error> reflectStage(
    std::span<const std::uint32_t> spirv, bool isVertex) {
  ReflectModule module;
  if (!module.create(spirv)) {
    return fail("SPIR-V reflection failed: invalid shader module");
  }
  if (isVertex) {
    if (auto r = checkVertexInputs(module.get()); !r) {
      return std::unexpected(r.error());
    }
  }

  std::uint32_t setCount = 0;
  spvReflectEnumerateDescriptorSets(&module.get(), &setCount, nullptr);
  std::vector<SpvReflectDescriptorSet *> sets(setCount);
  spvReflectEnumerateDescriptorSets(&module.get(), &setCount, sets.data());

  MaterialLayout layout;
  for (const SpvReflectDescriptorSet *set : sets) {
    if (set->set == 0) {
      continue; // Per-frame data, owned by the Renderer.
    }
    if (set->set != 1) {
      return fail("Descriptor set " + std::to_string(set->set) +
                  " is not supported (only set 0 and set 1)");
    }
    for (std::uint32_t i = 0; i < set->binding_count; ++i) {
      const SpvReflectDescriptorBinding &binding = *set->bindings[i];
      if (binding.binding == 0) {
        if (binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
          return fail("Set 1 binding 0 must be the material uniform block");
        }
        layout.blockSize = binding.block.padded_size;
        for (std::uint32_t m = 0; m < binding.block.member_count; ++m) {
          const SpvReflectBlockVariable &member = binding.block.members[m];
          if (member.name == nullptr) {
            continue;
          }
          const ParamType type = decodeType(member);
          if (type != ParamType::Unsupported &&
              member.absolute_offset + paramSize(type) > layout.blockSize) {
            return fail(std::string("Material parameter '") + member.name +
                        "' does not fit in its block");
          }
          layout.params.push_back({member.name, type, member.absolute_offset});
        }
        continue;
      }
      if (binding.descriptor_type !=
              SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
          binding.image.dim != SpvDim2D || binding.image.arrayed != 0 ||
          binding.count != 1 || binding.name == nullptr) {
        return fail("Set 1 binding " + std::to_string(binding.binding) +
                    " must be a named sampler2D");
      }
      layout.textures.push_back({binding.name, binding.binding});
    }
  }
  std::ranges::sort(layout.textures, {}, &TextureSlotInfo::binding);
  return layout;
}
} // namespace

std::uint32_t paramSize(ParamType type) {
  switch (type) {
  case ParamType::Float:
  case ParamType::Int:
    return 4;
  case ParamType::Vec2:
    return 8;
  case ParamType::Vec3:
    return 12;
  case ParamType::Vec4:
    return 16;
  case ParamType::Mat4:
    return 64;
  case ParamType::Unsupported:
    return 0;
  }
  return 0;
}

const char *paramTypeName(ParamType type) {
  switch (type) {
  case ParamType::Float:
    return "float";
  case ParamType::Int:
    return "int";
  case ParamType::Vec2:
    return "vec2";
  case ParamType::Vec3:
    return "vec3";
  case ParamType::Vec4:
    return "vec4";
  case ParamType::Mat4:
    return "mat4";
  case ParamType::Unsupported:
    return "unsupported";
  }
  return "unsupported";
}

const ParamInfo *MaterialLayout::find(std::string_view name) const {
  for (const ParamInfo &param : params) {
    if (param.name == name) {
      return &param;
    }
  }
  return nullptr;
}

int MaterialLayout::findTexture(std::string_view name) const {
  for (std::size_t i = 0; i < textures.size(); ++i) {
    if (textures[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::expected<MaterialLayout, Error>
reflectMaterialLayout(std::span<const std::uint32_t> vertexSpirv,
                      std::span<const std::uint32_t> fragmentSpirv) {
  auto vertex = reflectStage(vertexSpirv, true);
  if (!vertex) {
    return std::unexpected(vertex.error());
  }
  auto fragment = reflectStage(fragmentSpirv, false);
  if (!fragment) {
    return std::unexpected(fragment.error());
  }

  // Merge: the uniform block may appear in both stages but must be identical;
  // texture slots are the union (same binding => same name).
  MaterialLayout merged = std::move(*fragment);
  if (vertex->blockSize > 0) {
    if (merged.blockSize == 0) {
      merged.blockSize = vertex->blockSize;
      merged.params = std::move(vertex->params);
    } else {
      bool same = merged.blockSize == vertex->blockSize &&
                  merged.params.size() == vertex->params.size();
      for (std::size_t i = 0; same && i < merged.params.size(); ++i) {
        same = merged.params[i].name == vertex->params[i].name &&
               merged.params[i].type == vertex->params[i].type &&
               merged.params[i].offset == vertex->params[i].offset;
      }
      if (!same) {
        return fail(
            "Vertex and fragment shader declare different material blocks");
      }
    }
  }
  for (TextureSlotInfo &slot : vertex->textures) {
    const auto existing = std::ranges::find(merged.textures, slot.binding,
                                            &TextureSlotInfo::binding);
    if (existing == merged.textures.end()) {
      merged.textures.push_back(std::move(slot));
    } else if (existing->name != slot.name) {
      return fail("Set 1 binding " + std::to_string(slot.binding) +
                  " has different names in the vertex and fragment shader");
    }
  }
  std::ranges::sort(merged.textures, {}, &TextureSlotInfo::binding);
  return merged;
}
} // namespace Paper
