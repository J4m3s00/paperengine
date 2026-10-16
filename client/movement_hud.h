#pragma once

#include "base/cvar.h"
#include "core/renderer/camera.h"
#include "game/collision.h"
#include "game/movement.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

// Movement debugging: a big speed readout (like CS speed meters), a window with
// the full player state and a speed graph, and optional in-world vectors
// (velocity, ground normal) projected onto the screen. Call `draw` from the UI
// callback.
class MovementHud {
public:
  explicit MovementHud(Paper::CvarRegistry &registry = Paper::cvars())
      : m_showSpeed(registry.addBool("cl_showspeed", true, "Show the speed readout")),
        m_showTrace(registry.addBool("cl_debug_trace", false,
                                     "Draw velocity and ground normal in the world")) {}

  void setDetailsVisible(bool visible) { m_details = visible; }

  void draw(const Paper::PlayerState &state, const Paper::Camera &camera,
            const Paper::CollisionWorld &world) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float hspeed = std::hypot(state.velocity.x, state.velocity.z);
    m_history[m_next] = hspeed;
    m_next = (m_next + 1) % m_history.size();

    // Ground probe: shows the surface under the player even when it is too steep
    // to stand on (surf), where onGround is false.
    const Paper::TraceResult ground =
        world.traceHull(state.origin, state.origin - glm::vec3(0.0F, 2.0F, 0.0F),
                        Paper::hullOf(state));

    if (m_showSpeed.value()) {
      char text[32];
      std::snprintf(text, sizeof(text), "%.0f", hspeed);
      ImDrawList *draw = ImGui::GetForegroundDrawList();
      const float size = 28.0F;
      const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0F, text);
      const ImVec2 pos(display.x * 0.5F - extent.x * 0.5F, display.y * 0.78F);
      draw->AddText(ImGui::GetFont(), size, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), text);
      // Green on the ground, white in the air.
      draw->AddText(ImGui::GetFont(), size, pos,
                    state.onGround ? IM_COL32(120, 255, 120, 255) : IM_COL32(255, 255, 255, 255), text);
    }

    if (m_showTrace.value()) {
      drawVector(camera, display, state.origin + glm::vec3(0.0F, 36.0F, 0.0F),
                 state.velocity * 0.25F, IM_COL32(80, 220, 255, 255));
      if (ground.fraction < 1.0F) {
        const bool walkable = ground.normal.y >= 0.7F;
        drawVector(camera, display, ground.endPos, ground.normal * 40.0F,
                   walkable ? IM_COL32(80, 255, 80, 255) : IM_COL32(255, 80, 80, 255));
      }
    }

    if (!m_details) {
      return;
    }
    ImGui::SetNextWindowPos(ImVec2(display.x - 430.0F, 10.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0F, 300.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Movement")) {
      ImGui::Text("speed   %7.1f  (vertical %.1f)", hspeed, state.velocity.y);
      ImGui::Text("origin  %.1f %.1f %.1f", state.origin.x, state.origin.y, state.origin.z);
      ImGui::Text("ground %s   ducked %s   jump held %s", state.onGround ? "yes" : "no",
                  state.ducked ? "yes" : "no", state.jumpHeld ? "yes" : "no");
      if (ground.fraction < 1.0F) {
        ImGui::Text("surface under feet: normal %.2f %.2f %.2f (%s)", ground.normal.x,
                    ground.normal.y, ground.normal.z,
                    ground.normal.y >= 0.7F ? "walkable" : "too steep");
      } else {
        ImGui::TextUnformatted("surface under feet: none");
      }
      ImGui::PlotLines("##speed", m_history.data(), static_cast<int>(m_history.size()),
                       static_cast<int>(m_next), "horizontal speed", 0.0F, 600.0F, ImVec2(0.0F, 80.0F));
      bool trace = m_showTrace.value();
      if (ImGui::Checkbox("draw velocity / ground normal (cl_debug_trace)", &trace)) {
        m_showTrace.set(trace);
      }
    }
    ImGui::End();
  }

private:
  // Draws a world-space segment from `from` along `delta`, projected with the
  // same projection the renderer uses.
  static void drawVector(const Paper::Camera &camera, const ImVec2 &display,
                         const glm::vec3 &from, const glm::vec3 &delta, ImU32 color) {
    const glm::mat4 proj = glm::perspectiveRH_ZO(camera.fovY, display.x / display.y,
                                                 camera.nearPlane, camera.farPlane);
    const glm::mat4 viewProj = proj * camera.view();
    glm::vec2 a;
    glm::vec2 b;
    if (!project(viewProj, from, display, a) || !project(viewProj, from + delta, display, b)) {
      return;
    }
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, 2.0F);
    draw->AddCircleFilled(ImVec2(b.x, b.y), 3.0F, color);
  }

  static bool project(const glm::mat4 &viewProj, const glm::vec3 &p, const ImVec2 &display,
                      glm::vec2 &out) {
    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0F);
    if (clip.w <= 0.001F) {
      return false;
    }
    const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
    out = {(ndc.x * 0.5F + 0.5F) * display.x, (1.0F - (ndc.y * 0.5F + 0.5F)) * display.y};
    return true;
  }

  Paper::Cvar<bool> &m_showSpeed;
  Paper::Cvar<bool> &m_showTrace;
  std::array<float, 240> m_history{};
  std::size_t m_next = 0;
  bool m_details = false;
};
