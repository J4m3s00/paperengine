# M3: Combat in the dev world (size L)

Shooting at stationary bots on the dev map.

## Scope

- Data-driven weapon definitions (JSON): damage, rate of fire, armor penetration, range falloff, spray pattern, inaccuracy parameters (movement, jump, crouch, shot count), recovery, magazine/reload, draw time. Adding a weapon means adding data, not code, unless it needs a new behaviour.
- Slice weapons: AK-47, M4A4, AWP, Glock-18, USP-S, Deagle, knife.
- `HitboxProvider` (ADR 0011) with fixed hitboxes, pose-history ring buffer, server-side lag compensation (rewind to the shooter's view tick).
- Hitscan trace, damage, armor, headshot multiplier, death/respawn, stationary bot entities.
- View-model rendering (separate FOV and depth range), muzzle flash, tracer, impact decals as simple effects.
- Basic audio with miniaudio: shots, footsteps, impacts, 3D positional.
- Client-side prediction of firing (recoil/spray, ammo), server-confirmed hits.

## Exit gate

- Spray patterns match the reference pattern data within tolerance; replay tests for weapons.
- Hits register correctly at 100 ms RTT against moving targets (lag compensation test with bot harness).
- Penetration is out of scope for the first playable and tracked as a follow-up.
