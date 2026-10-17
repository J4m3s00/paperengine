# M5: Editor and map compiler (size L)

- In-engine `editor` target (ImGui): convex brush editing (planes, face/vertex manipulation), entity placement (spawns, bombsites), material/texture browser.
- Map compiler: bakes collision data (brushes, BVH), render meshes, visibility data (PVS or portals) and lightmaps.
- Map format moves from hand-written JSON to the editor's saved format, with the JSON loader kept for tests.

Exit gate: a map built in the editor loads and plays in client and server with identical collision.

## Status: editor done, compiler reduced

- **Format** (`game/map.h`): brushes keep how they were authored (box, ramp, planes); `serializeMap`/`saveMap` write the same JSON v1 the game loads, so "a map built in the editor" and "a hand-written map" are the same thing. Round trips are tested for the dev world and the arena, including identical collision traces.
- **Editing model** (`game/map_edit.h`, headless, tested): add/remove/duplicate/move/resize brushes and entities, grouped undo/redo, ray picking, validation (collision builds, spawns exist, nothing stuck inside a brush or floating).
- **Editor** (`editor/`, ImGui): fly camera (hold right mouse, WASD, Space/Ctrl, Shift, scroll for speed), click to select, outliner, properties (box mins/maxs, ramp fields, material, entity type/origin/yaw), grid snap (1..128), `G` to grab and move on the ground, arrows and PageUp/PageDown nudge, Delete, Ctrl+Z/Ctrl+Y, Ctrl+S, Add menu (box, floor slab, wall, ramp, entities), Open/Save-as dialogs, live "Checks" window, playtest in the game (starts `client --offline --map <file>`), an `.unsaved` copy if you close with changes. `--screenshot out.png --select-brush N` renders without a display.
- **`mapc`** (`editor/mapc.cpp`): command-line check that prints errors, warnings and the map's **collision checksum**. Its first run found two spawns in the dev world standing on the edge of a surf ramp (fixed).
- **Same collision on both sides:** the server sends the checksum in `Accept`; the client refuses to play when its copy of the map differs ("Your copy of map X differs from the server's"). That also closes the map-rotation gap noted in M4.
- `mapview/` (new library): brush faces to meshes and the placeholder material palette, shared by client and editor.

Not done (deliberately):
- **No separate baked map format, PVS or lightmaps.** The maps are a few dozen brushes, the renderer is CPU-driven and unprofiled at scale, and the plan says to profile before adding culling. Baking would only add a second format to keep in sync. Revisit with real map sizes in M6 (issue #48 stays open for that).
- Vertex/face manipulation of free-form plane brushes (planes brushes can be moved, duplicated and deleted, boxes and ramps can be resized by value), no mouse gizmo handles, no texture browser beyond the material-name list, no multi-select, no bombsites (no bomb mode yet).
- The editor has been verified headless/by screenshot and through unit tests of the model; mouse interaction (click select, grab, fly) is untested by a human.
