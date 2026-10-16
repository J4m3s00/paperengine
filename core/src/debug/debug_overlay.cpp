#include "core/debug/debug_overlay.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace Paper {
void DebugOverlay::draw(float frameSeconds) {
  m_frameMs[m_next] = frameSeconds * 1000.0F;
  m_next = (m_next + 1) % kHistory;
  m_count = std::min(m_count + 1, kHistory);
  if (!m_visible) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(10.0F, 10.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380.0F, 420.0F), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Debug", &m_visible)) {
    drawFrameTimes();
    ImGui::SeparatorText("Cvars");
    drawCvars();
  }
  ImGui::End();
}

void DebugOverlay::drawFrameTimes() const {
  if (m_count == 0) {
    return;
  }
  std::vector<float> sorted(m_frameMs.begin(), m_frameMs.begin() + m_count);
  std::ranges::sort(sorted);
  float sum = 0.0F;
  for (const float ms : sorted) {
    sum += ms;
  }
  const float avgMs = sum / static_cast<float>(m_count);
  // 1% low: FPS at the 99th percentile frame time.
  const float p99Ms = sorted[std::min(m_count - 1, m_count * 99 / 100)];
  ImGui::Text("%.2f ms  %.0f FPS (avg)", avgMs, avgMs > 0.0F ? 1000.0F / avgMs : 0.0F);
  ImGui::Text("1%% low: %.0f FPS (%.2f ms)", p99Ms > 0.0F ? 1000.0F / p99Ms : 0.0F, p99Ms);
  ImGui::PlotLines("##frames", m_frameMs.data(), static_cast<int>(kHistory),
                   static_cast<int>(m_next), nullptr, 0.0F, 2.0F * std::max(avgMs, 1.0F),
                   ImVec2(0.0F, 70.0F));
}

void DebugOverlay::drawCvars() {
  for (CvarBase *cvar : m_registry.all()) {
    ImGui::PushID(cvar->name().c_str());
    if (auto *b = dynamic_cast<Cvar<bool> *>(cvar)) {
      bool value = b->value();
      if (ImGui::Checkbox(cvar->name().c_str(), &value)) {
        b->set(value);
      }
    } else if (auto *i = dynamic_cast<Cvar<int> *>(cvar)) {
      int value = i->value();
      if (ImGui::DragInt(cvar->name().c_str(), &value, 1.0F, i->min(), i->max())) {
        i->set(value);
      }
    } else if (auto *f = dynamic_cast<Cvar<float> *>(cvar)) {
      float value = f->value();
      const float range = f->max() - f->min();
      if (ImGui::DragFloat(cvar->name().c_str(), &value, range / 500.0F, f->min(), f->max())) {
        f->set(value);
      }
    }
    if (!cvar->description().empty() && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s (default %s)", cvar->description().c_str(),
                        cvar->defaultString().c_str());
    }
    ImGui::PopID();
  }
}
} // namespace Paper
