# OpenSimFarm

OpenSimFarm is an independent, open-source re-implementation of the 1993 farm simulation game
SimFarm, written in C++17 with SDL2. It runs on Linux, Windows and in the browser.

Website and downloads: **https://alissonkn.github.io/opensimfarm/**

**You need your own copy of SimFarm** (DOS or Windows edition). This repository contains no game files; the engine
reads them from your installation at startup, without copying or changing them. It contains no code from the
original program: it was written from scratch, from a description of how the game behaves and how its files are laid
out.

Does OpenSimFarm use AI? *YES*. It's 2026, of course it does.

## Features
* The farm, crops, animals, machines, weather, disasters, market, bank and town, as in the original
* Loads and saves the original's saved games
* Larger worlds (144 x 144) and a map editor in Design Your Own
* English, Portuguese (Brazil) and Spanish; more can be added without programming
* Zoom and scrolling at any window size; a touch layout for phones in the browser version
* A radio in the menu bar, plus seven new songs composed for OpenSimFarm
* A Funds list of every cash change, and every crop available from the start
* Optional extras: smart machine driving, "Modern Sound" and "Modern Render"

## Install
Downloads: https://github.com/alissonkn/opensimfarm/releases/latest
- **Windows**: `OpenSimFarm-VERSION-windows-x64-setup.exe` (no administrator rights needed), or the plain zip.
- **Linux**: `.rpm` (Fedora, openSUSE ...) and `.deb` (Debian 12+, Ubuntu 22.04+).
- **Browser**: https://alissonkn.github.io/opensimfarm/play/ — your SimFarm files are read locally, never uploaded.

## Run
```
opensimfarm /path/to/SimFarm             # first start (the folder is remembered)
opensimfarm                              # later starts
opensimfarm /path/to/SimFarm MYFARM.SFM  # open a saved game
opensimfarm --reconfigure                # choose the SimFarm folder again
```
The SimFarm folder is the one holding `MEGADATA.IDX` and `MEGADATA.EEA`: an installed DOS or Windows game, or the
`SIMFARM` folder of the Windows CD. A Windows CD with only a setup program works too (choose the CD; the files are
unpacked once). Without a folder, OpenSimFarm searches the usual places and then asks.

Settings and saves live in `~/.config/opensimfarm/` (Linux) or `%APPDATA%\OpenSimFarm\` (Windows).

## Licence
GNU General Public License version 3; see [engine/LICENSE.txt](engine/LICENSE.txt).

SimFarm is a trademark of its respective owner. This project is not affiliated with or endorsed by
the owners of SimFarm.
