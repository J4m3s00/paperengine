#pragma once

#include "mapview/asset_paths.h"
#include "weapon_models.h"

#include "anim/animator.h"
#include "base/cvar.h"
#include "core/renderer/renderer.h"
#include "core/scene/scene.h"
#include "net/protocol.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

// The player model: Quaternius' mannequin with its animation set, shared by
// every player. Per player there is a `Character` (skinned mesh entities, an
// animator and the weapon in the hand).
//
// The hitboxes (ADR 0011) do not follow the animation: they are the fixed
// boxes of the movement pose. The model is only the picture.
class CharacterAssets {
public:
  CharacterAssets(Paper::Renderer &renderer, Paper::ShaderHandle skinnedShader)
      : m_renderer(renderer) {
    auto model = renderer.loadModel(Paper::characterModelPath(), skinnedShader);
    if (!model) {
      spdlog::warn("character model not loaded: {}", model.error().message);
      return;
    }
    m_model = *model;
    const Paper::Model &loaded = renderer.model(*model);
    if (!loaded.animations || loaded.animations->clips.empty() || loaded.materials.size() < 2) {
      spdlog::warn("character model has no skeleton or animations");
      m_model.reset();
      renderer.destroyModel(*model);
      return;
    }
    m_library = loaded.animations;
    m_bodyMaterial = loaded.materials[0];
    // One look per team: the model's two materials (body, joints) recolored.
    const auto make = [&](const glm::vec3 &color) {
      const auto material = renderer.createMaterial(skinnedShader, {});
      if (!material) {
        return loaded.materials[0];
      }
      (void)renderer.setMaterialParam(*material, "baseColorFactor", glm::vec4(color, 1.0F));
      (void)renderer.setMaterialParam(*material, "metallicFactor", 0.0F);
      (void)renderer.setMaterialParam(*material, "roughnessFactor", 0.85F);
      (void)renderer.setMaterialParam(*material, "emissiveFactor", glm::vec3(0.0F));
      (void)renderer.setMaterialParam(*material, "normalScale", 1.0F);
      (void)renderer.setMaterialParam(*material, "occlusionStrength", 1.0F);
      (void)renderer.setMaterialParam(*material, "alphaCutoff", 0.5F);
      m_ownMaterials.push_back(*material);
      return *material;
    };
    m_bodyNone = make({0.55F, 0.6F, 0.55F});
    m_bodyT = make({0.78F, 0.5F, 0.18F});
    m_bodyCT = make({0.22F, 0.4F, 0.78F});
    m_joints = make({0.12F, 0.12F, 0.14F});

    const auto find = [&](const char *name) { return m_library->findClip(name); };
    m_idle = find("Idle_Loop");
    m_walk = find("Walk_Loop");
    m_jog = find("Jog_Fwd_Loop");
    m_sprint = find("Sprint_Loop");
    m_crouchIdle = find("Crouch_Idle_Loop");
    m_crouchMove = find("Crouch_Fwd_Loop");
    m_jump = find("Jump_Loop");
    m_death = find("Death01");
    m_aimNeutral = find("Pistol_Aim_Neutral");
    m_aimUp = find("Pistol_Aim_Up");
    m_aimDown = find("Pistol_Aim_Down");
    m_shoot = find("Pistol_Shoot");
    m_upperBody = Paper::maskBelow(m_library->skeleton, "spine_01");
    m_hand = m_library->skeleton.find("hand_r");
  }
  ~CharacterAssets() {
    for (const auto material : m_ownMaterials) {
      m_renderer.destroyMaterial(material);
    }
    if (m_model) {
      m_renderer.destroyModel(*m_model);
    }
  }
  CharacterAssets(const CharacterAssets &) = delete;
  CharacterAssets &operator=(const CharacterAssets &) = delete;

  [[nodiscard]] bool ok() const { return m_model.has_value(); }
  [[nodiscard]] const Paper::Model &model() const { return m_renderer.model(*m_model); }
  [[nodiscard]] const Paper::AnimationLibrary &library() const { return *m_library; }
  [[nodiscard]] Paper::MaterialHandle body(Paper::Team team) const {
    return team == Paper::Team::T ? m_bodyT : team == Paper::Team::CT ? m_bodyCT : m_bodyNone;
  }
  [[nodiscard]] Paper::MaterialHandle bodySource() const { return m_bodyMaterial; }
  [[nodiscard]] Paper::MaterialHandle joints() const { return m_joints; }

  int m_idle = -1, m_walk = -1, m_jog = -1, m_sprint = -1, m_crouchIdle = -1, m_crouchMove = -1,
      m_jump = -1, m_death = -1, m_aimNeutral = -1, m_aimUp = -1, m_aimDown = -1, m_shoot = -1;
  int m_hand = -1;
  std::vector<float> m_upperBody;

private:
  Paper::Renderer &m_renderer;
  std::optional<Paper::ModelHandle> m_model;
  std::shared_ptr<const Paper::AnimationLibrary> m_library;
  Paper::MaterialHandle m_bodyMaterial;
  Paper::MaterialHandle m_bodyNone, m_bodyT, m_bodyCT, m_joints;
  std::vector<Paper::MaterialHandle> m_ownMaterials;
};

// Sets a Transform from a (rigid + uniform scale) matrix.
inline void setTransformFromMatrix(Paper::Transform &t, const glm::mat4 &m) {
  t.position = glm::vec3(m[3]);
  const glm::vec3 sx(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                     glm::length(glm::vec3(m[2])));
  t.scale = sx;
  if (sx.x > 1e-6F && sx.y > 1e-6F && sx.z > 1e-6F) {
    t.rotation = glm::quat_cast(
        glm::mat3(glm::vec3(m[0]) / sx.x, glm::vec3(m[1]) / sx.y, glm::vec3(m[2]) / sx.z));
  }
}

// One player's body.
class Character {
public:
  // Meters (the model) to Source units.
  static constexpr float kScale = 39.3701F;

  Character(Paper::Scene &scene, const CharacterAssets &assets, WeaponModels &weapons,
            const Paper::WeaponDb &db, Paper::CvarRegistry &cvars = Paper::cvars())
      : m_scene(scene), m_assets(assets), m_weapons(weapons), m_db(db),
        m_animator(assets.library()), m_cvars(&cvars) {
    m_instance = scene.instantiateSkinned(assets.model());
    for (const entt::entity e : m_instance.meshes) {
      auto &mr = scene.registry().get<Paper::MeshRenderer>(e);
      m_isBody.push_back(mr.material.index == assets.bodySource().index);
    }
    m_guns.fill(entt::null);
    hide();
  }

  void hide() {
    m_scene.registry().get<Paper::Transform>(m_instance.root).scale = glm::vec3(0.0F);
    for (const entt::entity gun : m_guns) {
      if (gun != entt::null) {
        m_scene.registry().get<Paper::Transform>(gun).scale = glm::vec3(0.0F);
      }
    }
    m_visible = false;
  }

  void shot() { m_shotPending = true; }

  void update(float dt, const Paper::NetPlayer &p) {
    auto &root = m_scene.registry().get<Paper::Transform>(m_instance.root);
    m_visible = true;
    // The model faces +Z; the game's yaw 0 looks along -Z.
    root.position = p.state.origin;
    root.rotation = glm::angleAxis(p.yaw + 3.14159265F, glm::vec3(0.0F, 1.0F, 0.0F));
    root.scale = glm::vec3(kScale);

    for (std::size_t i = 0; i < m_instance.meshes.size(); ++i) {
      auto &mr = m_scene.registry().get<Paper::MeshRenderer>(m_instance.meshes[i]);
      mr.material = m_isBody[i] ? m_assets.body(p.info.team) : m_assets.joints();
    }

    chooseAnimation(p);
    m_animator.update(dt);
    m_animator.computeMatrices(m_skin, &m_world);
    for (const entt::entity e : m_instance.meshes) {
      m_scene.registry().get<Paper::SkinPose>(e).joints = m_skin;
    }
    attachWeapon(p, root);
  }

private:
  void chooseAnimation(const Paper::NetPlayer &p) {
    const float speed = std::hypot(p.state.velocity.x, p.state.velocity.z);
    int clip = m_assets.m_idle;
    float rate = 1.0F;
    if (!p.vitals.alive) {
      clip = m_assets.m_death;
    } else if (!p.state.onGround) {
      clip = m_assets.m_jump;
    } else if (p.state.ducked) {
      clip = speed > 20.0F ? m_assets.m_crouchMove : m_assets.m_crouchIdle;
      rate = speed > 20.0F ? std::clamp(speed / 85.0F, 0.6F, 1.5F) : 1.0F;
    } else if (speed > 200.0F) {
      clip = m_assets.m_sprint;
      rate = std::clamp(speed / 250.0F, 0.7F, 1.3F);
    } else if (speed > 125.0F) {
      clip = m_assets.m_jog;
      rate = std::clamp(speed / 190.0F, 0.7F, 1.3F);
    } else if (speed > 15.0F) {
      clip = m_assets.m_walk;
      rate = std::clamp(speed / 100.0F, 0.6F, 1.4F);
    }
    // Walking backwards plays the cycle in reverse.
    const glm::vec3 forward(-std::sin(p.yaw), 0.0F, -std::cos(p.yaw));
    const float along = p.state.velocity.x * forward.x + p.state.velocity.z * forward.z;
    if (along < -0.3F * speed && speed > 15.0F) {
      rate = -rate;
    }
    m_animator.playKeepingPhase(clip, 0.18F);
    m_animator.setSpeed(rate);

    // Upper body: aim along the view (pitch picks the pose), flinch on a shot.
    if (!p.vitals.alive) {
      m_animator.playOverlay(-1, {});
      m_aimClip = -1;
      return;
    }
    if (m_shotPending && m_assets.m_shoot >= 0) {
      m_animator.playOverlay(m_assets.m_shoot, m_assets.m_upperBody, 0.04F, false);
      m_aimClip = -2;
      m_shotPending = false;
    } else if (!m_animator.overlayActive() || (m_aimClip >= -1 && m_aimClip != aimFor(p))) {
      m_aimClip = aimFor(p);
      m_animator.playOverlay(m_aimClip, m_assets.m_upperBody, 0.12F, true);
    }
  }

  [[nodiscard]] int aimFor(const Paper::NetPlayer &p) const {
    return p.pitch > 0.35F ? m_assets.m_aimUp : p.pitch < -0.35F ? m_assets.m_aimDown
                                                                  : m_assets.m_aimNeutral;
  }

  void attachWeapon(const Paper::NetPlayer &p, const Paper::Transform &root) {
    const std::size_t weapon = std::min<std::size_t>(p.weapon, m_db.size() - 1);
    for (std::size_t i = 0; i < m_guns.size(); ++i) {
      if (m_guns[i] != entt::null && i != weapon) {
        m_scene.registry().get<Paper::Transform>(m_guns[i]).scale = glm::vec3(0.0F);
      }
    }
    if (m_assets.m_hand < 0 || !p.vitals.alive) {
      if (m_guns[weapon] != entt::null) {
        m_scene.registry().get<Paper::Transform>(m_guns[weapon]).scale = glm::vec3(0.0F);
      }
      return;
    }
    if (m_guns[weapon] == entt::null) {
      m_guns[weapon] = m_weapons.spawn(weapon);
    }
    if (m_guns[weapon] == entt::null) {
      return;
    }
    // Hand matrix in model space, then the same placement as the body.
    const glm::mat4 character = root.matrix();
    const glm::mat4 hand = m_world[static_cast<std::size_t>(m_assets.m_hand)];
    // Tuned by eye (cvars cl_gunhand_*): the gun's grip sits in the palm with
    // the barrel pointing where the character looks.
    setTransformFromMatrix(m_scene.registry().get<Paper::Transform>(m_guns[weapon]),
                           character * hand * gripOffset());
  }

  [[nodiscard]] glm::mat4 gripOffset() const {
    const auto get = [&](const char *name, float fallback) {
      auto *cvar = dynamic_cast<Paper::Cvar<float> *>(m_cvars->find(name));
      return cvar != nullptr ? cvar->value() : fallback;
    };
    glm::mat4 m = glm::translate(glm::mat4(1.0F), glm::vec3(get("cl_gunhand_x", 0.0F),
                                                           get("cl_gunhand_y", 0.0F),
                                                           get("cl_gunhand_z", 0.0F)));
    m = glm::rotate(m, glm::radians(get("cl_gunhand_rx", 0.0F)), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(get("cl_gunhand_ry", 0.0F)), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(get("cl_gunhand_rz", 0.0F)), glm::vec3(0, 0, 1));
    return m;
  }

  Paper::Scene &m_scene;
  const CharacterAssets &m_assets;
  WeaponModels &m_weapons;
  const Paper::WeaponDb &m_db;
  Paper::CvarRegistry *m_cvars;
  Paper::Scene::SkinnedInstance m_instance;
  std::vector<bool> m_isBody;
  Paper::Animator m_animator;
  std::vector<glm::mat4> m_skin;
  std::vector<glm::mat4> m_world;
  std::array<entt::entity, Paper::kMaxWeapons> m_guns;
  int m_aimClip = -1;
  bool m_shotPending = false;
  bool m_visible = false;
};
