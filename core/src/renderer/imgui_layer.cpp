#include "renderer/imgui_layer.h"

#include "core/vulkan_context.h"
#include "core/window.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cmath>

namespace Paper {
namespace {
constexpr std::uint32_t kDescriptorPoolSize = 16;
}

std::expected<std::unique_ptr<ImGuiLayer>, Error>
ImGuiLayer::create(const Window &window, const VulkanContext &context,
                   VkFormat colorFormat, VkFormat depthFormat,
                   std::uint32_t imageCount) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  // No imgui.ini next to the executable.
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  // The swapchain is sRGB, so ImGui's sRGB-authored colors must be linearized
  // or the UI looks washed out.
  for (ImVec4 &color : ImGui::GetStyle().Colors) {
    color.x = std::pow(color.x, 2.2F);
    color.y = std::pow(color.y, 2.2F);
    color.z = std::pow(color.z, 2.2F);
  }

  if (!ImGui_ImplGlfw_InitForVulkan(window.nativeHandle(), true)) {
    ImGui::DestroyContext();
    return std::unexpected(
        Error{ErrorCode::Window, "ImGui GLFW backend init failed"});
  }

  // The scene pass has a depth attachment, so the UI pipeline must declare it.
  VkPipelineRenderingCreateInfoKHR rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = depthFormat;

  ImGui_ImplVulkan_InitInfo info{};
  info.ApiVersion = VK_API_VERSION_1_3;
  info.Instance = context.instance();
  info.PhysicalDevice = context.physicalDevice();
  info.Device = context.device();
  info.QueueFamily = context.graphicsQueueFamily();
  info.Queue = context.graphicsQueue();
  info.DescriptorPoolSize = kDescriptorPoolSize;
  info.MinImageCount = 2;
  info.ImageCount = imageCount;
  info.UseDynamicRendering = true;
  info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;
  if (!ImGui_ImplVulkan_Init(&info)) {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return std::unexpected(
        Error{ErrorCode::Vulkan, "ImGui Vulkan backend init failed"});
  }
  return std::unique_ptr<ImGuiLayer>(new ImGuiLayer());
}

ImGuiLayer::~ImGuiLayer() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void ImGuiLayer::beginFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiLayer::endFrame() { ImGui::Render(); }

void ImGuiLayer::record(VkCommandBuffer cmd) const {
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

bool ImGuiLayer::wantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }
bool ImGuiLayer::wantsKeyboard() const {
  return ImGui::GetIO().WantCaptureKeyboard;
}
} // namespace Paper
