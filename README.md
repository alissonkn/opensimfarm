# OpenSimFarm

OpenSimFarm is an independent, open-source re-implementation of the 1993 farm simulation game
SimFarm, written in C++17 with SDL2. It runs on modern Linux and Windows.

Website and downloads: **https://alissonkn.github.io/opensimfarm/**

**You need your own copy of SimFarm.** This repository contains no game files: no pictures, sounds,
music, maps or texts. At startup the engine reads them from your own SimFarm installation, without copying or
changing them.

The engine contains no code from the original program. It was written from scratch, from a description of how the
game behaves and how its files are laid out.

## Features
* The farm, its crops, animals, machines, weather, disasters, market, bank and town, as in the original
* Loads and saves the original's saved games
* Zoom (mouse wheel or the + / − buttons) and moving around with the arrow keys or WASD, at any window size
* A Funds list of every change to your cash (click the funds), and every crop available from the start (the Plant
  list and the market show all crop files)
* Window scaling, plus optional extras: smart driving for machines, "Modern Sound" (the game's own sounds,
  cleaned up and played in stereo, and the music played with new instruments) and "Modern Render" (the game's own
  pictures smoothed, and animals, machines and cars gliding from cell to cell)

## Install
Packages are built by `engine/tools/package.sh` into `engine/dist/`:
- Windows: `OpenSimFarm-VERSION-windows-x64-setup.exe` installs for the current user (no administrator rights),
  finds your SimFarm folder or lets you choose it, and adds Start menu and desktop shortcuts. The uninstaller
  keeps your saves and settings. A plain zip is built as well.
- Linux: `opensimfarm-VERSION-1.x86_64.rpm` (Fedora, openSUSE ...) and `opensimfarm_VERSION-1_amd64.deb`
  (Debian 12+, Ubuntu 22.04+). They install `/usr/bin/opensimfarm` with a menu entry and icon, and pull in SDL2.

None of them contains anything of the original game. How to build them (tools, one command, checks):
[engine/README.md, "Building the installers"](engine/README.md#building-the-installers).

## Build
Linux (needs CMake and the SDL2 development package, e.g. `libsdl2-dev` or `SDL2-devel`):
```
cd engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
If the SDL2 headers are not installed system-wide, pass `-DSDL2_ROOT=/path/to/SDL2`.

Windows (Visual Studio 2019+ or MinGW), with the SDL2 development package (`SDL2-devel-2.x-VC.zip`):
```
cd engine
cmake -S . -B build -DSDL2_ROOT=C:\path\to\SDL2-2.x
cmake --build build --config Release
```

## Run
```
opensimfarm /path/to/SimFarm             # first start: where your SimFarm is installed (remembered afterwards)
opensimfarm                              # later starts
opensimfarm /path/to/SimFarm MYFARM.SFM  # open a saved game
```
At the first start without a folder, OpenSimFarm looks for SimFarm in the usual places (your home folder, the
drive roots on Windows, `/opt`, `/media` ...). If that fails, it asks you to choose the folder in the system's folder
dialog (on Linux this needs zenity or kdialog). The folder is remembered. `opensimfarm --findgame` prints what
the search finds.
Settings and saved games are kept in `~/.config/opensimfarm/` (Linux) or `%APPDATA%\OpenSimFarm\` (Windows).

## Tests
```
./build/opensimfarm --selftest      # saved-game round trips
./build/opensimfarm --toolstest     # editing tools
./build/opensimfarm --newgametest   # world creation
./build/opensimfarm --audiotest     # sound and music rendering
./build/opensimfarm --graphicstest  # picture smoothing and movement
```

## Licence
GNU General Public License version 3; see [engine/LICENSE.txt](engine/LICENSE.txt).

SimFarm is a trademark of its respective owner. This project is not affiliated with or endorsed by
the owners of SimFarm.
