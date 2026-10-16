# 0009: Authoritative server netcode on ENet

Status: accepted

## Decision

- One dedicated, authoritative server simulates the game. Clients send timestamped `usercmd`s and never decide positions, hits or damage.
- The local player uses client-side prediction with reconciliation (replay unacknowledged usercmds on top of the server state). Remote players are rendered with entity interpolation. The server rewinds hitboxes for lag compensation when resolving shots (ADR 0011).
- Server tick is 128 Hz, snapshots go out at 64 Hz by default (configurable per server), the client sends one usercmd per rendered frame.
- Transport is ENet (reliable and unreliable channels over UDP). Snapshot encoding, delta compression, bit-packing and ack/baseline tracking are our own layer on top, because that is where netcode quality is decided.
- The protocol can withhold entities per client from day one, even though the first version replicates everything (basis for anti-wallhack culling). The server validates usercmd rate, plausibility and movement caps. No third-party anti-cheat.
- Limits for sizing: 16 player slots, 10 in normal play.

## Considered options

GameNetworkingSockets: more features (encryption, congestion control) but a heavy dependency; we only need the transport. Peer-to-peer and rollback: wrong fit for a competitive shooter with hitscan weapons. Raw UDP: more work with no gain over ENet.

## Consequences

- Input latency of the local player does not depend on RTT.
- Every gameplay rule must run inside the shared simulation (ADR 0012) so the client can predict it.
- Joining, matchmaking and encryption are not covered; direct connect only.
