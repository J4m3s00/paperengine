# M6: Polish and performance (size L)

- Skeletal animation and player models; switch the `HitboxProvider` to skeleton hitboxes only if profiling and feel require it (ADR 0011).
- Rendering: culling from map visibility data, lightmaps, simple shadows, GPU-driven draws only if profiling asks for them.
- Penetration and wall damage, grenades and utility, economy.
- Final perf passes against the README targets on reference hardware, including the M1 Mac target.

Exit gate: README performance targets are met and recorded.

## Status: skeletal animation, art and first perf numbers done; culling deliberately not

Done:
- **GPU skinning** (`core`): `Vertex` gained joint indices and unorm8 weights (locations 4/5, ignored by static shaders); a per-frame joint palette (storage buffer, set 0 binding 1) holds the skinning matrices of every `SkinPose` entity; `pbr_skinned.vert` reads them via `gl_InstanceIndex` (the draw's firstInstance is the palette offset), static meshes drawn with it stay put (zero weights).
- **`anim` library** (headless, `anim_tests`): `Skeleton`, `AnimationClip`, `sampleClip`, pose blending/masks, `Animator` (cross-fades, phase-keeping locomotion switches, looping and one-shot overlays on part of the body).
- **Importers**: glTF skins and animations (fastgltf) and **FBX** (ufbx; static meshes, node tree, materials; the kit's palette texture is applied when the FBX names none). `Renderer::loadModel` picks the importer by extension.
- **Art**: Quaternius mannequin + animation library (43 clips) for every player (T/CT colors, walk/jog/sprint speeds from movement, crouch, jump, death, pistol aim pose by view pitch, shoot flinch from shot events, weapon in the right hand); Styloo gun models for the view model and the held weapon; the prototyping kit as map **props** (`props` in the map JSON, drawn by client and editor, optional invisible "collision" box added by the editor) and a new map `prototype` built from it.
- Players are drawn as the model; `cl_showhitboxes 1` shows the exact (fixed) hitboxes instead. Hitboxes stay fixed per ADR 0011: they are not driven by the animation, so what you hit can differ slightly from the picture (the decision to switch to skeleton hitboxes needs the profiling below).
- `r_vsync 0` presents without waiting for the display (mailbox/immediate).

Measured (release, Apple M4, 1280x720, vsync off, prototype map, 15 bots + the client, CPU side):
- per frame: 0.33 ms update (animation and skin matrices of 15 players, weapons, effects) + 1.9 ms transforms and render submission, about 2.2 ms in total. The loop ran at that pace (about 450 FPS average), so the GPU kept up. `PAPER_BENCH_FRAMES=N` prints this.
- With vsync on the loop is simply display bound.

Not done / gaps:
- **No culling or occlusion**: the data above gives no reason for it at these map sizes (draw list is sorted, ~100 draws); revisit with bigger maps or weaker GPUs. Not measured on the GTX 1660/RX 580 targets or on Windows/Linux.
- Strafing and backpedaling reuse the forward cycle (backwards plays it in reverse); there are no rifle animations, so every weapon uses the pistol aim pose; no reload/hit-reaction animation on remote players (the server does not replicate reload state to others); the first-person view has no arms.
- M4A4, Glock, USP-S and Deagle use stand-in gun models (the pack has AK, AWP, MAC-10, shotgun and a small blaster only); the knife has no model.
- Environment: pieces are one palette color each (the kit's texture is an 8x8 palette), ramps and stairs props have no collision helper (use ramp brushes), wall pieces with doors need hand-made collision.
- The `assets/` folder (111 MB, all three packs including unused FBX copies) is in the repository; the installer ships only what the game uses.
