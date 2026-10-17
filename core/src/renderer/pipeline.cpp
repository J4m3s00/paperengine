#include "pipeline.h"

#include "core/renderer/mesh.h"

#include "vk_check.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Paper {
namespace {
VkCullModeFlags toVk(CullMode mode) {
  switch (mode) {
  case CullMode::None:
    return VK_CULL_MODE_NONE;
  case CullMode::Back:
    return VK_CULL_MODE_BACK_BIT;
  case CullMode::Front:
    return VK_CULL_MODE_FRONT_BIT;
  }
  return VK_CULL_MODE_BACK_BIT;
}
} // namespace

std::expected<VkPipeline, Error>
createGraphicsPipeline(const PipelineInputs &in) {
  // Fragment stage: ALPHA_MASK specialization constant (constant_id = 0).
  const VkBool32 alphaMask = in.state.alphaMode == AlphaMode::Mask;
  const VkSpecializationMapEntry specEntry{0, 0, sizeof(VkBool32)};
  VkSpecializationInfo specInfo{};
  specInfo.mapEntryCount = 1;
  specInfo.pMapEntries = &specEntry;
  specInfo.dataSize = sizeof(VkBool32);
  specInfo.pData = &alphaMask;

  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = in.vertex;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = in.fragment;
  stages[1].pName = "main";
  stages[1].pSpecializationInfo = &specInfo;

  const VkVertexInputBindingDescription vertexBinding{
      0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  const std::array<VkVertexInputAttributeDescription, 6> vertexAttributes{{
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
      {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
      {4, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Vertex, joints)},
      {5, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, weights)},
  }};
  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &vertexBinding;
  // Joint attributes only when the shader reads them (keeps validation quiet).
  vertexInput.vertexAttributeDescriptionCount =
      static_cast<std::uint32_t>(in.skinned ? vertexAttributes.size() : 4);
  vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Right-handed, Y up: the negative-height viewport flips Y, so
  // counter-clockwise triangles are front faces (see CONTEXT.md).
  VkPipelineRasterizationStateCreateInfo rasterization{};
  rasterization.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = toVk(in.state.cullMode);
  rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth = 1.0F;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = in.state.depthTest ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = in.state.depthTest ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.maxDepthBounds = 1.0F;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &blendAttachment;

  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount =
      static_cast<std::uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &in.colorFormat;
  rendering.depthAttachmentFormat = in.depthFormat;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = &rendering;
  pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterization;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlend;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = in.layout;

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (auto r = vkCheck(vkCreateGraphicsPipelines(in.device, VK_NULL_HANDLE, 1,
                                                 &pipelineInfo, nullptr,
                                                 &pipeline),
                       ErrorCode::Vulkan, "vkCreateGraphicsPipelines");
      !r) {
    return std::unexpected(r.error());
  }
  return pipeline;
}
} // namespace Paper
