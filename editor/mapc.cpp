// mapc: checks a map the way the game will see it.
//
//   mapc map.json [map2.json ...]
//
// Loads each map, runs the editor's checks (collision builds, spawns exist,
// nothing stuck or floating) and prints the collision checksum. Client and
// server compare that checksum when connecting, so two machines can tell
// they have the same map. Exit code 1 if any map has an error.
#include "game/map_edit.h"

#include <spdlog/spdlog.h>

#include <cstdio>

int main(int argc, char **argv) {
  if (argc < 2) {
    spdlog::error("usage: mapc map.json [more maps]");
    return 2;
  }
  int failures = 0;
  for (int i = 1; i < argc; ++i) {
    auto map = Paper::loadMap(argv[i]);
    if (!map) {
      spdlog::error("{}", map.error().message);
      ++failures;
      continue;
    }
    const Paper::MapEditor editor(*map);
    int errors = 0;
    int warnings = 0;
    for (const Paper::MapIssue &issue : editor.validate()) {
      (issue.error ? errors : warnings) += 1;
      std::printf("%s: %s: %s\n", argv[i], issue.error ? "error" : "warning", issue.text.c_str());
    }
    std::printf("%s: %zu brushes, %zu entities, %d errors, %d warnings, collision checksum %08x\n",
                argv[i], map->brushes.size(), map->entities.size(), errors, warnings,
                Paper::mapChecksum(*map));
    failures += errors > 0 ? 1 : 0;
  }
  return failures > 0 ? 1 : 0;
}
