
## Status: implemented (with known gaps)

- `weapons/*.json` + `WeaponDb` (`game/weapon_def.h`): AK-47, M4A4, AWP, Glock-18, USP-S, Deagle, knife. Adding a weapon is adding a file (max 8; `order` defines index and number key).
- `game/hitbox.h`: `HitboxProvider` + `FixedHitboxProvider` (7 boxes, crouch scaling), `rayVsHitboxes`. `game/lag_compensation.h`: 128-tick pose history, interpolated rewind, clamped to 0.2 s.
- `game/weapon.h`: shared weapon state machine (fire rate in 1/256-tick fixed point on the usercmd clock, ammo, reload, draw, semi/auto, spray pattern, stance/move/air/per-shot inaccuracy, seeded spread), damage and armor math.
- Server (`net/server.h`): hitscan resolved against rewound hitboxes after all players moved, world occlusion, armor (legs unprotected), death/respawn (3 s), server bots (`addBot`, stationary or scripted-moving), reliable `GameEvent`s (Shot to everyone but the shooter, Hit to attacker and victim, Kill to all). Snapshots carry health/armor/alive/weapon for everyone and the recipient's full weapon state.
- Client: `ClientSession` predicts weapons with reconciliation (same `stepWeapon`); `client/combat_view.h` draws view model (placeholder box), tracers, impact markers, crosshair, hit/kill markers, HUD, kill feed; `audio/` (miniaudio, 32 voices, 3D, procedural placeholder sounds) plays shots, footsteps, impacts, hit ticks, reload.
- Recoil kick is smooth: each shot sets `punchTarget` (the pattern position) and `punch` follows it exponentially (`recoil.smoothing`, default 0.04 s, per weapon in the JSON); the bullet leaves along the view as it is when the trigger fires, then the kick builds up.
- Controls online: mouse 1 fire, R reload, 1-7 weapons; offline respawn moved to T.

Measured / verified:
- Lag compensation test (100 ms RTT, 3 ms jitter, moving bot, aimed at the displayed position): 26/26 hits with rewind vs 9/39 without; stationary target 13/13 either way.
- 30 simulated minutes, 16 bots shooting, reloading and switching at 100 ms RTT, 2% loss: 71 corrections in 1.8 M snapshots (deaths/respawns account for the big ones), server tick 27 us average, 379 B per snapshot (own weapon state included), 23 KB/s down, 12 KB/s up per client.
- Spray patterns for all guns replayed shot by shot against the data; a golden weapon stream pins fire rate, ammo and recoil.

Known gaps:
- **Recoil patterns, inaccuracy values and hitbox sizes are plausible placeholders, not the CS:GO reference data.** The gate "spray patterns match reference data" needs that data; dropping it into `weapons/*.json` is all that is required.
- No AWP scope/zoom, no penetration (as planned), no friendly fire/teams, buy menu or economy (M4).
- The view model is a world-space box in front of the camera: no separate FOV/depth pass yet, so it can clip into walls.
- No decals (impact markers are small cubes), no muzzle flash geometry beyond one cube, no 3D weapon models; audio is synthesized noise and unheard by me (no ears here; the device initializes and the headless mixing is tested for panning, distance and voice limits).
- Hit feedback and kill feed are not predicted; they arrive with the reliable event (one RTT).
- Interactive feel of shooting is untested by a human.
