# M5: Editor and map compiler (size L)

- In-engine `editor` target (ImGui): convex brush editing (planes, face/vertex manipulation), entity placement (spawns, bombsites), material/texture browser.
- Map compiler: bakes collision data (brushes, BVH), render meshes, visibility data (PVS or portals) and lightmaps.
- Map format moves from hand-written JSON to the editor's saved format, with the JSON loader kept for tests.

Exit gate: a map built in the editor loads and plays in client and server with identical collision.
