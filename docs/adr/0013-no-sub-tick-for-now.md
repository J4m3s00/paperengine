# 0013: Whole-tick usercmds, no sub-tick timestamps yet

Status: accepted (revisit after M3 hit registration)

## Decision

A usercmd is one 128 Hz tick of input (7.8 ms). The server executes exactly one per tick per client, in order. Input is not timestamped within a tick.

## Why

- At 128 Hz the quantization is 7.8 ms, below one frame at 144 Hz, so sub-tick buys little for movement.
- Executing one cmd per tick makes speed hacking impossible by construction and keeps client prediction exactly replayable.
- Sub-tick matters mostly for shot timing; lag compensation (ADR 0011) rewinds to the cmd's tick, which is the same resolution.

## Consequences

- Revisit if M3 playtests show shots resolved a tick early/late. Adding a fraction field to the usercmd would not break the movement replay.
