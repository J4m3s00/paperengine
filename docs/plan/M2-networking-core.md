
## Status: implemented

- `net/`: `Transport` interface; `LoopbackNetwork` (virtual clock, deterministic conditioner); `EnetTransport` (ch 0 reliable, ch 1 unreliable sequenced); `ConditionedTransport` (same degradation for any real transport); `BitWriter/BitReader`; `protocol` (Hello/Accept/Reject, UserCmds with up to 8 redundant cmds, Snapshot with per-field delta against the last acked baseline); `Server` (transport-agnostic, one usercmd executed per tick per client, visibility hook); `ClientSession` (prediction with `quantizeUserCmd`, reconciliation by replay, smoothed corrections, interpolation, clock offset, usercmd-queue time scaling).
- Executables: `server` (ENet, real-time loop), `bots` (`--inprocess` virtual time with conditioner, or `--connect` over UDP), `client --connect host[:port] [--lag RTTms --jitter ms --loss p]` with a Network window.
- Measured (release, in-process, 16 bots, 50 ms one-way latency, 6 ms jitter, 2% loss, 30 simulated minutes, 9 s wall):
  - Server tick: 24.8 us average, 1.26 ms worst (budget 7.8 ms).
  - Corrections: 52 in 1.8 M snapshots (0.003%), all from starved ticks (late/lost usercmds); the per-minute count stays flat after the join phase, so no desync growth.
  - Bandwidth per client (16 players visible): 20.3 KB/s down (331 B per snapshot at 64 Hz), 7.6 KB/s up. With 10 players: 13.7 KB/s down.
- Not covered by the soak: real-time operation on a loaded machine, memory growth over 30 real minutes (all histories are bounded: 64 snapshots per client, 512 pending cmds).
- Known gaps: floats are sent unquantized (bandwidth can drop later, see M6); interpolation holds instead of extrapolating; no clock resync after long stalls; the client takes the map from `--map`, not from the server's Accept message; movement cvars must match on both sides (no cvar replication yet); the exit gate "feels the same at 100 ms RTT" needs a human playing with `--lag 100 --loss 0.02`.
