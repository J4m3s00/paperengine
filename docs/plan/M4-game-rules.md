# M4: Game rules (size M)

- Round state machine, teams, spawns, round timer, scoring, scoreboard.
- First real mode (bomb defusal or deathmatch, decided here).
- Buy phase and economy are out of the slice unless time allows.
- Minimal menu and UI: connect by IP, server browser is out of scope.
- Server config (cvars, map rotation) and a LAN/direct-connect workflow.

Exit gate: two or more players can play several full rounds against each other without desyncs.

## Status: implemented

First real mode (decided here, ADR 0015): **team elimination**: T vs CT, rounds with a freeze time, no respawns while a round is live, a round ends when a team is wiped out (or after the round time, a win for CT), first team to `mp_winlimit` rounds takes the match, then it starts over. Bomb defusal, buy phase and economy are not in the slice.

- `game/rules.h`: `RoundManager` (pure, tested), `MatchState`, `PlayerInfo`, `RulesCvars` (`mp_teamplay`, `mp_freezetime`, `mp_roundtime`, `mp_roundend_time`, `mp_matchend_time`, `mp_winlimit`, `mp_friendlyfire`, `mp_respawn_time`), `restrictCmd` (freeze: look only).
- Server: balanced teams, team spawns (`spawn_t` / `spawn_ct` entities, falling back to `spawn`), freeze/round flow, stats (kills, deaths), friendly fire off by default (bullets pass through teammates), match restart, map rotation (`--maps a,b,c`: after a match everyone gets "map change", clients reconnect and load the new map), `server.cfg` (`--config`, `--set cvar value`). Without `mp_teamplay` it is free-for-all with the map's bots as targets.
- Client: main menu (server, name, connect, offline sandbox), pause menu (Escape: resume, disconnect, quit), the map is loaded from the server's Accept message, round timer, scores, round and match banners, scoreboard (hold Tab), team markers over players, kill feed.
- Soak (release, in-process, 12 bots shooting in teams, 100 ms RTT, 2% loss, 20 simulated minutes): 34 rounds played, 12 of 12 clients agree with the server on phase, round and score at the end, server tick 18 us, 0.10% of snapshots caused a correction (round starts teleport everyone and the freeze end changes what predicted input does).

How to play on a LAN:
```
./build/release/server/server --config server.cfg --maps dev_world,arena   # one machine
./build/release/client/client                                                # type the server's IP in the menu
```
Port 27015/udp must be reachable. `client --connect IP:PORT --name N` skips the menu.

Known gaps: no buy phase or economy, no bomb, no half-time side swap, no spectating after death (the dead player just sees the world), no names beyond "Player N" on the scoreboard (the Hello name is not replicated yet), the rotation list needs client and server to have the same map files, a dead client sees no killcam.
