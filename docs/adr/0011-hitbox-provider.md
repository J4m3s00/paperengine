# 0011: Hitboxes behind a HitboxProvider

Status: accepted

## Decision

Hit detection is a first-class subsystem. The server stores a ring buffer of hitbox poses per player for the last N ticks (lag compensation). Damage and shot code only talks to a `HitboxProvider` interface that returns the hitbox set (head, chest, stomach, arms, legs) of a player at a given tick.

The first implementation derives a fixed hitbox set from movement state (position, eye height, crouch, a few pose variables). It does not depend on skeletal animation. A later implementation samples the animated skeleton, as CS does, without changing lag compensation or damage code.

## Consequences

- Netcode and gunplay can be finished before skeletal animation exists.
- Moving to bone-attached hitboxes means the server must sample animations for every player each tick; the interface is designed so that cost can be measured before switching.
