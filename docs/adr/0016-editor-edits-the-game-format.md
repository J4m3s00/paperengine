# 0016: The editor edits the game's own map format; no baked format yet

Status: accepted

## Decision

The editor loads and saves the same JSON v1 maps the client and server load. Brushes remember how they were authored (box, ramp, planes), so they stay editable. A command-line checker (`mapc`) validates a map and prints a collision checksum; the server sends the checksum when a client joins and the client refuses a mismatching map.

There is no baked/binary runtime map, visibility data or lightmaps for now.

## Why

- One format means "built in the editor" and "plays identically on client and server" hold by construction, and the existing collision/replay tests keep covering it.
- The checksum catches the real failure (two different copies of a map) without a build step.
- Maps are tens of brushes; load time and draw cost are nowhere near a problem. The plan says to profile before adding culling, and PVS/lightmaps need a renderer that can use them.

## Consequences

- If maps grow or profiling shows the need, a compile step can sit between the editor format and the runtime without changing the editor.
- Free-form plane brushes cannot be reshaped in the editor yet.
