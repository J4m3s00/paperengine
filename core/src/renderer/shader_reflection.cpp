#include "shader_reflection.h"

#include <spirv_reflect.h>

#include <optional>

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

// Returns the material block of this stage, or nullopt if it has none.
std::expected<std::optional<MaterialLayout>, Error>
reflectStage(std::span<const std::uint32_t> spirv, bool isVertex) {
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

  std::optional<MaterialLayout> layout;
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
      if (binding.binding != 0 ||
          binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        return fail("Set 1 binding " + std::to_string(binding.binding) +
                    " is not supported yet (only a uniform block at binding 0)");
      }
      MaterialLayout result;
      result.blockSize = binding.block.padded_size;
      for (std::uint32_t m = 0; m < binding.block.member_count; ++m) {
        const SpvReflectBlockVariable &member = binding.block.members[m];
        if (member.name == nullptr) {
          continue;
        }
        const ParamType type = decodeType(member);
        if (type != ParamType::Unsupported &&
            member.absolute_offset + paramSize(type) > result.blockSize) {
          return fail(std::string("Material parameter '") + member.name +
                      "' does not fit in its block");
        }
        result.params.push_back({member.name, type, member.absolute_offset});
      }
      layout = std::move(result);
    }
  }
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

  if (vertex->has_value() && fragment->has_value()) {
    const MaterialLayout &a = **vertex;
    const MaterialLayout &b = **fragment;
    bool same = a.blockSize == b.blockSize && a.params.size() == b.params.size();
    for (std::size_t i = 0; same && i < a.params.size(); ++i) {
      same = a.params[i].name == b.params[i].name &&
             a.params[i].type == b.params[i].type &&
             a.params[i].offset == b.params[i].offset;
    }
    if (!same) {
      return fail("Vertex and fragment shader declare different material blocks");
    }
  }
  if (fragment->has_value()) {
    return std::move(**fragment);
  }
  if (vertex->has_value()) {
    return std::move(**vertex);
  }
  return MaterialLayout{};
}
} // namespace Paper
