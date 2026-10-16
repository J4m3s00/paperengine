# 0010: Custom collision and strict float simulation

Status: accepted

## Decision

- Player movement follows Source/CS:GO (`PM_*`-style ground and air acceleration, friction, step-up, ramp clipping via `ClipVelocity`, tunable cvars) so air-strafing, bunnyhopping and surfing emerge from the math.
- Collision is a purpose-built module: players are axis-aligned hulls (standing, crouched), the world is convex brushes/planes plus a BVH for triangle meshes. API: `traceHull`, `traceRay`, `pointContents`. It never allocates during a trace.
- No general rigid-body physics engine (Jolt, Bullet) in the movement path.
- Simulation code in `game` is built with strict floating point (no fast-math, no FMA contraction) and avoids platform-dependent math. We do not aim for bit-exact determinism across CPUs or compilers: tiny client/server drift is corrected by reconciliation, as in Source.

## Considered options

Jolt or Bullet with a kinematic controller: faster to start, but the feel depends on Source-specific clipping details that a physics engine does not give us. Fixed-point math: bit-exact but costly in effort and feel.

## Consequences

- We own the tracing code and its bugs, and must test it (replay tests in M1).
- Dynamic physics (grenades, props) later needs a separate, simpler solution.
