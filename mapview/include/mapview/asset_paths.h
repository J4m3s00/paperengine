#pragma once

#include "base/paths.h"

#include <filesystem>
#include <string>

// Where the downloaded art lives. `PAPER_ASSETS_DIR` is the source tree's
// assets folder (baked in at configure time); a packaged build keeps an
// `assets` folder next to the executable.
namespace Paper {
inline std::filesystem::path assetsRoot() { return dataDir("assets", PAPER_ASSETS_DIR); }

// Quaternius "Universal Animation Library": the mannequin and 43 animations.
inline std::filesystem::path characterModelPath() {
  return assetsRoot() / "Universal Animation Library[Standard]" / "Unreal-Godot" /
         "UAL1_Standard.glb";
}

// Quaternius-style modular prototyping kit (FBX): walls, floors, stairs, ...
inline std::filesystem::path propPiecesDir() {
  return assetsRoot() / "Free 3D Modular Game Assets For Prototyping" / "Pieces";
}

// Styloo gun pack (normal-mapped GLB versions).
inline std::filesystem::path gunModelPath(const std::string &stem) {
  return assetsRoot() / "Styloo Guns Asset Pack GLTF FBX V1.1" / "Normal version Color and NormalMap" /
         "GLB" / (stem + ".glb");
}
} // namespace Paper
