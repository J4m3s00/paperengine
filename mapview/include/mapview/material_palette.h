#pragma once

#include "core/assets/image.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The placeholder look of the maps: material names, their colors and the grid
// texture that makes world scale readable.
// Light grid on white so world-space scale is readable while moving.
inline Paper::Image makeGrid(std::uint32_t size, std::uint32_t cells) {
  Paper::Image image{size, size, std::vector<std::uint8_t>(size * size * 4)};
  const std::uint32_t cell = size / cells;
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const bool line = x % cell < 2 || y % cell < 2;
      const std::uint8_t v = line ? 150 : 235;
      std::uint8_t *px = &image.pixels[(y * size + x) * 4];
      px[0] = px[1] = px[2] = v;
      px[3] = 255;
    }
  }
  return image;
}

inline glm::vec3 colorFor(const std::string &material) {
  static const std::map<std::string, glm::vec3> palette{
      {"floor", {0.45F, 0.47F, 0.5F}},   {"wall", {0.6F, 0.55F, 0.5F}},
      {"stairs", {0.7F, 0.6F, 0.4F}},    {"platform", {0.6F, 0.35F, 0.35F}},
      {"ramp", {0.3F, 0.5F, 0.75F}},     {"surf", {0.2F, 0.75F, 0.55F}},
      {"box", {0.85F, 0.65F, 0.2F}},     {"pit", {0.2F, 0.2F, 0.25F}},
      {"ceiling", {0.5F, 0.4F, 0.6F}},   {"pillar", {0.8F, 0.4F, 0.2F}}};
  const auto it = palette.find(material);
  return it != palette.end() ? it->second : glm::vec3(0.6F);
}

// Material names the game knows (the editor offers these).
inline const std::vector<std::string> &materialNames() {
  static const std::vector<std::string> names{"floor", "wall",    "stairs", "platform", "ramp",
                                              "surf",  "box",     "pit",    "ceiling",  "pillar"};
  return names;
}
