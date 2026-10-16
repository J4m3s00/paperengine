#pragma once

#include "base/error.h"
#include "core/renderer/material.h"

#include <vulkan/vulkan.h>

#include <expected>

namespace Paper {
struct PipelineInputs {
  VkDevice device;
  VkPipelineLayout layout;
  VkShaderModule vertex;
  VkShaderModule fragment;
  RenderState state;
  VkFormat colorFormat;
  VkFormat depthFormat;
};

// Graphics pipeline for the fixed engine vertex layout (ADR 0004): dynamic
// rendering, dynamic viewport/scissor, counter-clockwise front faces.
[[nodiscard]] std::expected<VkPipeline, Error>
createGraphicsPipeline(const PipelineInputs &inputs);
} // namespace Paper
