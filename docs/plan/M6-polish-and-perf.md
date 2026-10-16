# M6: Polish and performance (size L)

- Skeletal animation and player models; switch the `HitboxProvider` to skeleton hitboxes only if profiling and feel require it (ADR 0011).
- Rendering: culling from map visibility data, lightmaps, simple shadows, GPU-driven draws only if profiling asks for them.
- Penetration and wall damage, grenades and utility, economy.
- Final perf passes against the README targets on reference hardware, including the M1 Mac target.

Exit gate: README performance targets are met and recorded.
