#pragma once

#include "character.h"
#include "weapon_models.h"

#include "audio/audio.h"
#include "audio/synth.h"
#include "core/renderer/camera.h"
#include "core/renderer/renderer.h"
#include "core/scene/components.h"
#include "core/scene/scene.h"
#include "game/collision.h"
#include "net/client_session.h"

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <deque>
#include <string>
#include <utility>
#include <vector>

// What combat looks like on the client: a placeholder view model, tracers and
// impact markers (scene entities that live a fraction of a second), and the
// HUD (crosshair, hit marker, health/armor/ammo, kill feed). Consumes the
// session's shot and event queues each frame.
class CombatView {
public:
  struct Assets {
    Paper::MeshHandle cube; // a 1x1x1 cube centered on the origin
    Paper::MaterialHandle gun;
    Paper::MaterialHandle tracer;
    Paper::MaterialHandle impact;
    Paper::MaterialHandle teamT;
    Paper::MaterialHandle teamCT;
  };

  // The gun models for the view model; without them the placeholder box shows.
  void setWeaponModels(WeaponModels *models) { m_models = models; }
  // Players whose shots arrived since the last call (for their animation).
  [[nodiscard]] std::vector<std::uint8_t> takeShooters() { return std::exchange(m_shooters, {}); }

  // Optional: without it (no sound device) everything is silent.
  void setAudio(Paper::AudioEngine *audio) {
    m_audio = audio;
    if (!audio) {
      return;
    }
    const std::uint32_t rate = audio->sampleRate();
    using namespace Paper::Synth;
    const auto add = [&](const std::vector<float> &s) { return audio->addSound(s, rate); };
    m_rifle = add(shot(ShotKind::Rifle, rate));
    m_pistol = add(shot(ShotKind::Pistol, rate));
    m_sniper = add(shot(ShotKind::Sniper, rate));
    m_knife = add(shot(ShotKind::Knife, rate));
    m_step = add(footstep(rate));
    m_impactSound = add(impact(rate));
    m_hitSound = add(hitMarker(rate));
    m_reloadSound = add(reload(rate));
  }

  CombatView(Paper::Scene &scene, const Assets &assets) : m_scene(scene) {
    m_gun = makeEntity(assets.cube, assets.gun);
    m_flash = makeEntity(assets.cube, assets.impact);
    for (std::size_t i = 0; i < m_markers.size(); ++i) {
      m_markers[i] = {makeEntity(assets.cube, assets.teamT), makeEntity(assets.cube, assets.teamCT)};
    }
    for (int i = 0; i < kPool; ++i) {
      m_tracers.push_back({makeEntity(assets.cube, assets.tracer), 0.0F});
      m_impacts.push_back({makeEntity(assets.cube, assets.impact), 0.0F});
    }
  }

  // Hides everything this view put into the scene (when leaving a match).
  void hideAll() {
    for (const entt::entity e : {m_gun, m_flash}) {
      transform(e).scale = glm::vec3(0.0F);
    }
    for (const entt::entity e : m_viewGuns) {
      if (e != entt::null) {
        transform(e).scale = glm::vec3(0.0F);
      }
    }
    for (auto &pair : m_markers) {
      transform(pair.first).scale = glm::vec3(0.0F);
      transform(pair.second).scale = glm::vec3(0.0F);
    }
    for (auto *pool : {&m_tracers, &m_impacts}) {
      for (Timed &t : *pool) {
        t.life = 0.0F;
        transform(t.entity).scale = glm::vec3(0.0F);
      }
    }
    m_killFeed.clear();
    m_banner.age = 1000.0F;
  }

  // Call once per frame before the scene is rendered. `view` is the camera
  // the player sees through.
  void update(float dt, const Paper::Camera &view, Paper::ClientSession &session,
              const Paper::CollisionWorld &world) {
    m_time += dt;
    if (m_audio) {
      m_audio->setListener(view.position, view.rotation * glm::vec3(0, 0, -1),
                           view.rotation * glm::vec3(0, 1, 0));
    }
    const glm::vec3 right = view.rotation * glm::vec3(1, 0, 0);
    const glm::vec3 up = view.rotation * glm::vec3(0, 1, 0);
    const glm::vec3 forward = view.rotation * glm::vec3(0, 0, -1);
    // The view model sits low and to the right; its barrel points along the view.
    const std::size_t weaponIndex = std::min<std::size_t>(session.combat().weapon, session.weapons().size() - 1);
    const Paper::WeaponDef &held = session.weapons().at(weaponIndex);
    const glm::vec3 gunAt = view.position + right * 6.0F - up * 6.0F + forward * (14.0F - 2.5F * m_kick);
    glm::vec3 muzzle = view.position + right * 3.5F - up * 3.5F + forward * 18.0F;
    entt::entity modelGun = entt::null;
    if (m_models != nullptr) {
      if (m_viewGuns[weaponIndex] == entt::null) {
        m_viewGuns[weaponIndex] = m_models->spawn(weaponIndex);
      }
      modelGun = m_viewGuns[weaponIndex];
    }
    // glTF guns point their barrel along +X; the view looks along -Z.
    const glm::mat4 gunMatrix = glm::translate(glm::mat4(1.0F), gunAt) * glm::mat4_cast(view.rotation) *
                                glm::rotate(glm::mat4(1.0F), glm::radians(90.0F), glm::vec3(0, 1, 0)) *
                                glm::scale(glm::mat4(1.0F), glm::vec3(kViewModelScale));
    if (modelGun != entt::null) {
      muzzle = glm::vec3(gunMatrix * glm::vec4(held.muzzle, 1.0F));
    }

    for (const Paper::ShotInfo &shot : session.takeLocalShots()) {
      const Paper::WeaponDef &def = session.weapons().at(shot.weapon);
      playShot(shot.weapon, session, shot.origin, /*own=*/true);
      if (def.melee) {
        m_kick = 1.0F;
        continue;
      }
      const auto trace = world.traceRay(shot.origin, shot.origin + shot.direction * def.range);
      const glm::vec3 end = trace.endPos;
      spawnTracer(muzzle, end);
      if (trace.fraction < 1.0F) {
        spawnImpact(end);
        if (m_audio) {
          m_audio->play(m_impactSound, end, 0.7F);
        }
      }
      m_kick = 1.0F;
      m_flashLeft = 0.04F;
    }
    for (const Paper::GameEvent &e : session.takeEvents()) {
      handle(e, session);
    }

    updateFootsteps(dt, session);
    updateMarkers(session);
    m_banner.age += dt;
    const bool reloading = session.combat().reloading();
    if (reloading && !m_wasReloading && m_audio) {
      m_audio->play2D(m_reloadSound, 0.8F);
    }
    m_wasReloading = reloading;

    m_kick = std::max(0.0F, m_kick - dt * 8.0F);
    m_hitMarker = std::max(0.0F, m_hitMarker - dt);
    m_flashLeft = std::max(0.0F, m_flashLeft - dt);
    for (auto &line : m_killFeed) {
      line.age += dt;
    }
    while (!m_killFeed.empty() && m_killFeed.front().age > 5.0F) {
      m_killFeed.pop_front();
    }
    fade(m_tracers, dt);
    fade(m_impacts, dt);

    const bool showGun = session.playing() && session.vitals().alive;
    for (std::size_t i = 0; i < m_viewGuns.size(); ++i) {
      if (m_viewGuns[i] != entt::null && (i != weaponIndex || !showGun)) {
        transform(m_viewGuns[i]).scale = glm::vec3(0.0F);
      }
    }
    if (modelGun != entt::null) {
      if (showGun) {
        setTransformFromMatrix(transform(modelGun), gunMatrix);
      }
      transform(m_gun).scale = glm::vec3(0.0F); // the box is only a stand-in
    } else {
      transform(m_gun).position = view.position + right * 4.0F - up * 4.0F +
                                  forward * (14.0F - 2.0F * m_kick);
      transform(m_gun).rotation = view.rotation;
      transform(m_gun).scale = showGun ? glm::vec3(1.4F, 2.2F, 10.0F) : glm::vec3(0.0F);
    }
    transform(m_flash).position = muzzle + forward * 2.0F;
    transform(m_flash).rotation = view.rotation;
    transform(m_flash).scale = showGun && m_flashLeft > 0.0F ? glm::vec3(3.0F) : glm::vec3(0.0F);
  }

  // Inside the UI callback. `scoreboard`: the scoreboard key is held.
  void drawHud(const Paper::ClientSession &session, bool scoreboard = false) {
    drawRoundHud(session);
    if (scoreboard) {
      drawScoreboard(session);
    }
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImVec2 c(size.x * 0.5F, size.y * 0.5F);
    const ImU32 white = IM_COL32(255, 255, 255, 230);
    const ImU32 shadow = IM_COL32(0, 0, 0, 160);
    if (session.vitals().alive) {
      // Crosshair: four ticks around a gap.
      constexpr float gap = 4.0F;
      constexpr float len = 7.0F;
      for (const ImU32 col : {shadow, white}) {
        const float o = col == shadow ? 1.0F : 0.0F;
        draw->AddLine(ImVec2(c.x - gap - len + o, c.y + o), ImVec2(c.x - gap + o, c.y + o), col, 2.0F);
        draw->AddLine(ImVec2(c.x + gap + o, c.y + o), ImVec2(c.x + gap + len + o, c.y + o), col, 2.0F);
        draw->AddLine(ImVec2(c.x + o, c.y - gap - len + o), ImVec2(c.x + o, c.y - gap + o), col, 2.0F);
        draw->AddLine(ImVec2(c.x + o, c.y + gap + o), ImVec2(c.x + o, c.y + gap + len + o), col, 2.0F);
      }
    }
    if (m_hitMarker > 0.0F) {
      const ImU32 col = m_hitWasKill ? IM_COL32(255, 60, 40, 255) : white;
      const float a = 6.0F;
      const float b = 14.0F;
      for (const float sx : {-1.0F, 1.0F}) {
        for (const float sy : {-1.0F, 1.0F}) {
          draw->AddLine(ImVec2(c.x + sx * a, c.y + sy * a), ImVec2(c.x + sx * b, c.y + sy * b), col, 2.0F);
        }
      }
    }

    const Paper::CombatState &combat = session.combat();
    const Paper::WeaponDef &def = session.weapons().at(combat.weapon);
    const Paper::WeaponAmmo &ammo = combat.ammo[combat.weapon];
    char buf[96];
    const float base = 40.0F;
    std::snprintf(buf, sizeof buf, "HP %d   AP %d", session.vitals().health, session.vitals().armor);
    drawText(draw, ImVec2(30.0F, size.y - base - 20.0F), 34.0F, buf,
             session.vitals().health > 30 ? white : IM_COL32(255, 80, 60, 255));
    if (def.melee) {
      std::snprintf(buf, sizeof buf, "%s", def.displayName.c_str());
    } else if (combat.reloading()) {
      std::snprintf(buf, sizeof buf, "%s   reloading...", def.displayName.c_str());
    } else {
      std::snprintf(buf, sizeof buf, "%s   %d / %d", def.displayName.c_str(), ammo.clip, ammo.reserve);
    }
    const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(34.0F, FLT_MAX, 0.0F, buf);
    drawText(draw, ImVec2(size.x - textSize.x - 30.0F, size.y - base - 20.0F), 34.0F, buf,
             !def.melee && ammo.clip == 0 ? IM_COL32(255, 80, 60, 255) : white);

    if (!session.vitals().alive) {
      const char *msg = "You died. Respawning...";
      const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(40.0F, FLT_MAX, 0.0F, msg);
      drawText(draw, ImVec2(c.x - t.x * 0.5F, c.y - 120.0F), 40.0F, msg, IM_COL32(255, 80, 60, 255));
    }
    float y = 20.0F;
    for (const auto &line : m_killFeed) {
      const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(22.0F, FLT_MAX, 0.0F, line.text.c_str());
      drawText(draw, ImVec2(size.x - t.x - 20.0F, y), 22.0F, line.text.c_str(), white);
      y += 26.0F;
    }
  }

private:
  static constexpr int kPool = 24;
  struct Timed {
    entt::entity entity;
    float life;
  };
  struct Banner {
    std::string text;
    ImU32 color = 0;
    float duration = 0.0F;
    float age = 1000.0F;
  };
  struct FeedLine {
    std::string text;
    float age = 0.0F;
  };

  entt::entity makeEntity(Paper::MeshHandle mesh, Paper::MaterialHandle material) {
    const auto e = m_scene.createEntity();
    m_scene.registry().emplace<Paper::MeshRenderer>(e, mesh, material);
    m_scene.registry().get<Paper::Transform>(e).scale = glm::vec3(0.0F);
    return e;
  }
  Paper::Transform &transform(entt::entity e) { return m_scene.registry().get<Paper::Transform>(e); }

  static void drawText(ImDrawList *draw, ImVec2 pos, float size, const char *text, ImU32 color) {
    draw->AddText(ImGui::GetFont(), size, ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, 200), text);
    draw->AddText(ImGui::GetFont(), size, pos, color, text);
  }

  void spawnTracer(const glm::vec3 &from, const glm::vec3 &to) {
    const glm::vec3 d = to - from;
    const float len = glm::length(d);
    if (len < 1.0F) {
      return;
    }
    Timed &t = m_tracers[m_nextTracer++ % m_tracers.size()];
    t.life = 0.06F;
    Paper::Transform &tr = transform(t.entity);
    tr.position = (from + to) * 0.5F;
    tr.rotation = Paper::lookRotation(d / len);
    tr.scale = glm::vec3(0.35F, 0.35F, len);
  }
  void spawnImpact(const glm::vec3 &at) {
    Timed &t = m_impacts[m_nextImpact++ % m_impacts.size()];
    t.life = 0.4F;
    Paper::Transform &tr = transform(t.entity);
    tr.position = at;
    tr.rotation = glm::quat(1, 0, 0, 0);
    tr.scale = glm::vec3(1.2F);
  }
  void fade(std::vector<Timed> &pool, float dt) {
    for (Timed &t : pool) {
      if (t.life > 0.0F) {
        t.life -= dt;
        if (t.life <= 0.0F) {
          transform(t.entity).scale = glm::vec3(0.0F);
        }
      }
    }
  }

  void playShot(std::uint8_t weapon, const Paper::ClientSession &session, const glm::vec3 &at,
                bool own) {
    if (!m_audio) {
      return;
    }
    const Paper::WeaponDef &def = session.weapons().at(weapon);
    const Paper::SoundId id = def.melee ? m_knife
                              : def.damage >= 100.0F ? m_sniper
                              : def.slot == Paper::WeaponSlot::Secondary ? m_pistol
                                                                         : m_rifle;
    if (own) {
      m_audio->play2D(id, 0.9F);
    } else {
      m_audio->play(id, at, 1.0F);
    }
  }

  // Steps while running on the ground: every ~0.35 s for the local player and
  // for each other player in view.
  void updateFootsteps(float dt, const Paper::ClientSession &session) {
    if (!m_audio) {
      return;
    }
    const auto step = [&](float &timer, const Paper::PlayerState &s, bool alive, bool local,
                          const glm::vec3 &at) {
      const float speed = std::hypot(s.velocity.x, s.velocity.z);
      // Walking (shift) and crouching are silent, like in CS.
      const bool loud = alive && s.onGround && !s.ducked && speed > 135.0F;
      timer = loud ? timer - dt : 0.0F;
      if (loud && timer <= 0.0F) {
        timer = 0.35F;
        if (local) {
          m_audio->play2D(m_step, 0.35F);
        } else {
          m_audio->play(m_step, at, 0.9F);
        }
      }
    };
    step(m_localStepTimer, session.predicted(), session.vitals().alive, true, glm::vec3(0.0F));
    for (const Paper::NetPlayer &p : session.remotePlayers()) {
      step(m_remoteStepTimer[p.slot], p.state, p.vitals.alive, false, p.state.origin);
    }
  }

  // Small colored marker over every other player's head: their team.
  void updateMarkers(const Paper::ClientSession &session) {
    for (auto &pair : m_markers) {
      transform(pair.first).scale = glm::vec3(0.0F);
      transform(pair.second).scale = glm::vec3(0.0F);
    }
    for (const Paper::NetPlayer &p : session.remotePlayers()) {
      if (!p.vitals.alive || p.info.team == Paper::Team::None) {
        continue;
      }
      auto &pair = m_markers[p.slot];
      const entt::entity e = p.info.team == Paper::Team::T ? pair.first : pair.second;
      const float top = p.state.ducked ? 54.0F : 72.0F;
      transform(e).position = p.state.origin + glm::vec3(0.0F, top + 10.0F, 0.0F);
      transform(e).scale = glm::vec3(5.0F);
    }
  }

  static const char *teamName(Paper::Team t) {
    return t == Paper::Team::T ? "Terrorists" : t == Paper::Team::CT ? "Counter-Terrorists" : "Players";
  }

  void drawRoundHud(const Paper::ClientSession &session) {
    const Paper::MatchState &m = session.match();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const float cx = size.x * 0.5F;
    char buf[96];
    const int secs = static_cast<int>(std::ceil(session.phaseSecondsLeft()));
    const char *phase = m.phase == Paper::Phase::Waiting    ? "Waiting for players"
                        : m.phase == Paper::Phase::Freeze   ? "Freeze"
                        : m.phase == Paper::Phase::Live     ? ""
                        : m.phase == Paper::Phase::RoundEnd ? "Round over"
                                                            : "Match over";
    std::snprintf(buf, sizeof buf, "T %d   %d:%02d   %d CT", m.scoreT, secs / 60, secs % 60, m.scoreCT);
    const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(30.0F, FLT_MAX, 0.0F, buf);
    drawText(draw, ImVec2(cx - t.x * 0.5F, 14.0F), 30.0F, m.phase == Paper::Phase::Waiting ? "" : buf,
             IM_COL32(255, 255, 255, 240));
    std::snprintf(buf, sizeof buf, "Round %d  %s", m.round, phase);
    const ImVec2 t2 = ImGui::GetFont()->CalcTextSizeA(20.0F, FLT_MAX, 0.0F, buf);
    drawText(draw, ImVec2(cx - t2.x * 0.5F, 48.0F), 20.0F,
             m.phase == Paper::Phase::Waiting ? phase : buf, IM_COL32(220, 220, 220, 220));

    if (m_banner.age < m_banner.duration) {
      const ImVec2 b = ImGui::GetFont()->CalcTextSizeA(48.0F, FLT_MAX, 0.0F, m_banner.text.c_str());
      drawText(draw, ImVec2(cx - b.x * 0.5F, size.y * 0.25F), 48.0F, m_banner.text.c_str(),
               m_banner.color);
    }
  }

  void drawScoreboard(const Paper::ClientSession &session) {
    std::vector<Paper::NetPlayer> players = session.latestPlayers();
    std::sort(players.begin(), players.end(), [](const auto &a, const auto &b) {
      if (a.info.team != b.info.team) return a.info.team > b.info.team;
      return a.info.kills > b.info.kills;
    });
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(size.x * 0.5F, size.y * 0.4F), ImGuiCond_Always, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowBgAlpha(0.8F);
    ImGui::Begin("Scoreboard", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
    const Paper::MatchState &m = session.match();
    ImGui::Text("Round %d   T %d : %d CT", m.round, m.scoreT, m.scoreCT);
    Paper::Team current = Paper::Team::None;
    bool open = false;
    for (const Paper::NetPlayer &p : players) {
      if (!open || p.info.team != current) {
        if (open) {
          ImGui::EndTable();
        }
        current = p.info.team;
        ImGui::Spacing();
        ImGui::TextColored(current == Paper::Team::T ? ImVec4(1.0F, 0.7F, 0.2F, 1.0F)
                                                     : ImVec4(0.4F, 0.7F, 1.0F, 1.0F),
                           "%s", teamName(current));
        ImGui::BeginTable(teamName(current), 4, ImGuiTableFlags_SizingFixedFit);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 200.0F);
        ImGui::TableSetupColumn("K", ImGuiTableColumnFlags_WidthFixed, 50.0F);
        ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 50.0F);
        ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 60.0F);
        open = true;
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%sPlayer %d", p.slot == session.slot() ? "> " : "", p.slot);
      ImGui::TableNextColumn();
      ImGui::Text("%d", p.info.kills);
      ImGui::TableNextColumn();
      ImGui::Text("%d", p.info.deaths);
      ImGui::TableNextColumn();
      if (p.vitals.alive) {
        ImGui::Text("%d", p.vitals.health);
      } else {
        ImGui::TextDisabled("dead");
      }
    }
    if (open) {
      ImGui::EndTable();
    }
    ImGui::End();
  }

  void handle(const Paper::GameEvent &e, const Paper::ClientSession &session) {
    switch (e.type) {
    case Paper::GameEvent::Type::Shot:
      spawnTracer(e.from, e.to);
      m_shooters.push_back(e.actor);
      playShot(e.weapon, session, e.from, /*own=*/false);
      break;
    case Paper::GameEvent::Type::Hit:
      if (e.actor == session.slot()) {
        m_hitMarker = 0.25F;
        m_hitWasKill = false;
        if (m_audio) {
          m_audio->play2D(m_hitSound, 0.6F, e.headshot ? 1.4F : 1.0F);
        }
      }
      break;
    case Paper::GameEvent::Type::RoundStart:
      m_banner = {"Round " + std::to_string(e.actor), IM_COL32(255, 255, 255, 255), 2.0F, 0.0F};
      break;
    case Paper::GameEvent::Type::RoundEnd:
      m_banner = {std::string(teamName(static_cast<Paper::Team>(e.actor))) + " win the round",
                  e.actor == 1 ? IM_COL32(255, 180, 60, 255) : IM_COL32(100, 180, 255, 255), 4.0F,
                  0.0F};
      break;
    case Paper::GameEvent::Type::MatchEnd:
      m_banner = {std::string(teamName(static_cast<Paper::Team>(e.actor))) + " win the match!",
                  IM_COL32(255, 230, 80, 255), 9.0F, 0.0F};
      break;
    case Paper::GameEvent::Type::Kill: {
      if (e.actor == session.slot()) {
        m_hitMarker = 0.4F;
        m_hitWasKill = true;
      }
      const auto name = [&](std::uint8_t slot) {
        return slot == session.slot() ? std::string("You") : "Player " + std::to_string(slot);
      };
      m_killFeed.push_back({name(e.actor) + " [" + session.weapons().at(e.weapon).displayName +
                                "]" + (e.headshot ? " (headshot) " : " ") + name(e.target),
                            0.0F});
      break;
    }
    }
  }

  Paper::Scene &m_scene;
  WeaponModels *m_models = nullptr;
  std::array<entt::entity, Paper::kMaxWeapons> m_viewGuns = [] {
    std::array<entt::entity, Paper::kMaxWeapons> a;
    a.fill(entt::null);
    return a;
  }();
  std::vector<std::uint8_t> m_shooters;
  static constexpr float kViewModelScale = 24.0F;
  Banner m_banner;
  std::array<std::pair<entt::entity, entt::entity>, Paper::kMaxPlayerSlots> m_markers;
  Paper::AudioEngine *m_audio = nullptr;
  Paper::SoundId m_rifle = 0, m_pistol = 0, m_sniper = 0, m_knife = 0, m_step = 0;
  Paper::SoundId m_impactSound = 0, m_hitSound = 0, m_reloadSound = 0;
  float m_localStepTimer = 0.0F;
  std::array<float, Paper::kMaxPlayerSlots> m_remoteStepTimer{};
  bool m_wasReloading = false;
  entt::entity m_gun;
  entt::entity m_flash;
  std::vector<Timed> m_tracers;
  std::vector<Timed> m_impacts;
  std::size_t m_nextTracer = 0;
  std::size_t m_nextImpact = 0;
  std::deque<FeedLine> m_killFeed;
  float m_time = 0.0F;
  float m_kick = 0.0F;
  float m_flashLeft = 0.0F;
  float m_hitMarker = 0.0F;
  bool m_hitWasKill = false;
};
