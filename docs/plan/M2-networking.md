# M2: Networking core (size L)

Authoritative server with prediction, per ADR 0009.

## Scope

- ENet transport wrapper plus a network conditioner (latency, jitter, loss, reorder, duplicate) for tests.
- Protocol: connect/handshake, usercmd packets (redundant recent cmds), snapshots with delta compression against the last acked baseline, bit-packing, per-client entity visibility hook.
- Headless server: 128 Hz fixed tick, players, snapshot sender at configurable rate, plausibility checks on usercmds.
- Client: prediction + reconciliation using the shared `game` movement, remote entity interpolation buffer, clock sync, smoothing of reconciliation corrections.
- Bot-client harness: N headless clients driven by scripts for soak tests and server perf benchmarks.
- Net debug overlay: RTT, loss, prediction errors, bandwidth, snapshot age.

## Exit gate

- Local movement feels the same at 0 and 100 ms RTT with 2% loss; corrections are rare and unobtrusive.
- 16-bot soak test runs 30 minutes without desync growth, leaks or tick overruns.
- Bandwidth per client is measured and documented.
- Decision recorded on whether sub-tick timestamps are needed.
