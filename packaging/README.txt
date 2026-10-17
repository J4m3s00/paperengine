Paper Engine
============

Play
  1. Run client.exe (double-click). A GPU with Vulkan 1.3 support and up-to-date
     drivers is required.
  2. The main menu lets you enter a server IP and your name, or start the
     offline sandbox.

Controls
  WASD + mouse to move and look, Mouse 1 fire, R reload, 1-7 weapons,
  Tab scoreboard, Escape menu, F1 debug overlay, T respawn (offline).

Host a server
  server.exe --config server.cfg --maps dev_world,arena
  Default UDP port: 27015. Forward the UDP port on your router for
  players outside your network.

Command line
  client.exe --connect host:port --name YourName
  client.exe --offline

https://github.com/J4m3s00/paperengine
