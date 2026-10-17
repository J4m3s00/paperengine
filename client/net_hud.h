#pragma once

#include "net/client_session.h"

#include <imgui.h>

#include <algorithm>
#include <array>

// Network debug window: connection state, input round trip, snapshot age,
// prediction corrections and bandwidth. Call `draw` from the UI callback.
class NetHud {
public:
  void draw(const Paper::ClientSession &session, float frameSeconds, double extraLatencyMs) {
    const Paper::ClientStats &s = session.stats();
    m_elapsed += frameSeconds;
    if (m_elapsed >= 1.0F) {
      m_downKBs = static_cast<double>(s.bytesReceived - m_lastDown) / 1024.0 / m_elapsed;
      m_upKBs = static_cast<double>(s.bytesSent - m_lastUp) / 1024.0 / m_elapsed;
      m_snapshotsPerSec = static_cast<double>(s.snapshotsReceived - m_lastSnapshots) / m_elapsed;
      m_lastDown = s.bytesReceived;
      m_lastUp = s.bytesSent;
      m_lastSnapshots = s.snapshotsReceived;
      m_elapsed = 0.0F;
    }
    if (s.corrections != m_lastCorrections) {
      m_correctionFlash = 1.0F;
      m_lastCorrections = s.corrections;
    }
    m_correctionFlash = std::max(0.0F, m_correctionFlash - frameSeconds);
    m_history[m_next++ % m_history.size()] = static_cast<float>(s.inputRttMs);

    ImGui::SetNextWindowPos(ImVec2(10.0F, 10.0F), ImGuiCond_FirstUseEver);
    ImGui::Begin("Network", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    static const char *names[] = {"connecting", "joining", "playing", "rejected", "disconnected"};
    ImGui::Text("state: %s (slot %d)", names[static_cast<int>(session.status())], session.slot());
    ImGui::Text("input RTT: %.0f ms%s", s.inputRttMs,
                extraLatencyMs > 0 ? " (simulated lag on)" : "");
    ImGui::Text("snapshot age: %.0f ms, %.0f snapshots/s", s.snapshotAgeMs, m_snapshotsPerSec);
    ImGui::Text("bandwidth: %.1f KB/s down, %.1f KB/s up", m_downKBs, m_upKBs);
    ImGui::TextColored(m_correctionFlash > 0 ? ImVec4(1, 0.4F, 0.3F, 1) : ImVec4(1, 1, 1, 1),
                       "corrections: %llu (last %.2f u)",
                       static_cast<unsigned long long>(s.corrections), s.lastCorrection);
    ImGui::Text("clock scale: %.3f", session.timeScale());
    ImGui::PlotLines("RTT", m_history.data(), static_cast<int>(m_history.size()),
                     static_cast<int>(m_next % m_history.size()), nullptr, 0.0F, FLT_MAX,
                     ImVec2(220, 40));
    ImGui::End();
  }

private:
  float m_elapsed = 0.0F;
  std::uint64_t m_lastDown = 0;
  std::uint64_t m_lastUp = 0;
  std::uint64_t m_lastSnapshots = 0;
  std::uint64_t m_lastCorrections = 0;
  double m_downKBs = 0.0;
  double m_upKBs = 0.0;
  double m_snapshotsPerSec = 0.0;
  float m_correctionFlash = 0.0F;
  std::array<float, 120> m_history{};
  std::size_t m_next = 0;
};
