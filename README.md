# OpenSimFarm

OpenSimFarm is an independent, open-source re-implementation of the 1993 farm simulation game
SimFarm, written in C++17 with SDL2. It runs on modern Linux and Windows.

**You need your own copy of SimFarm.** This repository contains no game files: no pictures, sounds,
music, maps or texts. At startup the engine reads them from your own SimFarm installation, without copying or
changing them.

The engine contains no code from the original program. It was written from scratch, from a description of how the
game behaves and how its files are laid out.

## Features
* The farm, its crops, animals, machines, weather, disasters, market, bank and town, as in the original
* Loads and saves the original's saved games
* Window scaling, plus optional extras: smart driving for machines and "Modern Sound" (the game's own sounds,
  cleaned up and played in stereo, and the music played with new instruments)

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
On Windows, drag your SimFarm folder onto `opensimfarm.exe` once, or put OpenSimFarm into the SimFarm folder.
Settings and saved games are kept in `~/.config/opensimfarm/` (Linux) or `%APPDATA%\OpenSimFarm\` (Windows).

## Tests
```
./build/opensimfarm --selftest      # saved-game round trips
./build/opensimfarm --toolstest     # editing tools
./build/opensimfarm --newgametest   # world creation
./build/opensimfarm --audiotest     # sound and music rendering
```

## Licence
GNU General Public License version 3; see [engine/LICENSE.txt](engine/LICENSE.txt).

SimFarm is a trademark of its respective owner. This project is not affiliated with or endorsed by
the owners of SimFarm.
