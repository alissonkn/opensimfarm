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
* Larger worlds: Design Your Own can make a world of 144 x 144 cells instead of 96 x 96 (the MAP button under the
  design picture), more than twice the land in the same plots of 8 x 8 cells. Everything else (fields, machines,
  animals, buildings) keeps the original's limits
* Saves written by OpenSimFarm end with a small signature that records the map size. The original game reads
  a 96 x 96 one as usual; a save without it (one from the original) opens as before and gets it the next time it is
  saved. A 144 x 144 world can only be played in OpenSimFarm
* Languages: English and Portuguese (Brazil), chosen with the Language button under the region map. The game's own
  words printed in its pictures are shown in the chosen language too. More languages can be added without
  programming (see [Languages](#languages))
* Zoom (mouse wheel or the + / − buttons) and moving around with the arrow keys or WASD, at any window size
* A radio at the right of the menu bar: skip back and forth through the game's songs, pause and resume the music,
  or leave the choice to the game (AUTO)
* A Funds list of every change to your cash (click the funds), and every crop available from the start (the Plant
  list and the market show all crop files)
* Window scaling, plus optional extras: smart driving for machines, "Modern Sound" (the game's own sounds,
  cleaned up and played in stereo, and the music played with new instruments) and "Modern Render" (the game's own
  pictures smoothed, and animals, machines and cars gliding from cell to cell)

## Install
Downloads: https://github.com/alissonkn/opensimfarm/releases/latest
- Windows: `OpenSimFarm-VERSION-windows-x64-setup.exe` installs for the current user (no administrator rights),
  finds your SimFarm folder or lets you choose it, and adds Start menu and desktop shortcuts. The uninstaller
  keeps your saves and settings. A plain zip is there as well.
- Linux: `opensimfarm-VERSION-1.x86_64.rpm` (Fedora, openSUSE ...) and `opensimfarm_VERSION-1_amd64.deb`
  (Debian 12+, Ubuntu 22.04+). They install `/usr/bin/opensimfarm` with a menu entry and icon, and pull in SDL2.

None of them contains anything of the original game. To build them yourself, see
[Building the installers](#building-the-installers).

Or play in the browser, with nothing to install: https://alissonkn.github.io/opensimfarm/play/ . You choose your
SimFarm folder on the page; its files are read on your computer and never uploaded.

> [!WARNING]
> **The browser version is a beta.** It is less tested than the download, may have bugs, and may not be as up to
> date as the desktop version, which stays the main one.

## Build
The source is in `engine/`. The Linux steps were tested on Fedora 44 and Ubuntu 24.04; the installers for both
systems are built on Linux. Build folders (`engine/build*`) and packages (`engine/dist/`) are not tracked.

### The desktop game
Linux, with a C++17 compiler, CMake 3.16+ and the SDL2 development package (`libsdl2-dev` on Debian/Ubuntu,
`SDL2-devel` or `sdl2-compat-devel` on Fedora):
```
cd engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/opensimfarm /path/to/SimFarm
```
If the SDL2 headers are not installed system-wide, unpack an SDL2 development package and pass
`-DSDL2_ROOT=/path/to/it`.

Windows (Visual Studio 2019+ or MinGW), with the SDL2 development package (`SDL2-devel-2.x-VC.zip`):
```
cd engine
cmake -S . -B build -DSDL2_ROOT=C:\path\to\SDL2-2.x
cmake --build build --config Release
```
and put `SDL2.dll` next to `opensimfarm.exe`.

Windows, cross-compiled on Linux (no Windows or MinGW needed): install Zig from pip
(`python3 -m pip install --user ziglang`), unpack the official `SDL2-devel-2.30.9-mingw.tar.gz`, then
```
cd engine
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=packaging/toolchains/windows.cmake -DCMAKE_BUILD_TYPE=Release \
      -DSDL2_ROOT=/path/to/SDL2-2.30.9/x86_64-w64-mingw32
cmake --build build-win
```

### Building the installers
`engine/tools/package.sh [windows] [rpm] [deb]` (no argument: all three) builds the release packages into
`engine/dist/`, the Windows ones included, on Linux. The version is the `project(OpenSimFarm VERSION ...)` number in
`engine/CMakeLists.txt`.

1. Tools, once. Fedora:
   ```
   sudo dnf install cmake gcc-c++ make rpm-build binutils 7zip mingw32-nsis sdl2-compat-devel python3-pillow python3-pip
   python3 -m pip install --user ziglang
   ```
   Debian 12+ / Ubuntu 22.04+:
   ```
   sudo apt install cmake g++ make rpm binutils p7zip-full nsis libsdl2-dev python3-pil python3-pip
   python3 -m pip install --user --break-system-packages ziglang     # Ubuntu 22.04: without --break-system-packages
   ```
2. SDL2 for Windows, once (its `SDL2.dll` goes into the installer):
   ```
   mkdir -p ~/tools/sdl2-mingw && cd ~/tools/sdl2-mingw
   curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz
   tar xzf SDL2-devel-2.30.9-mingw.tar.gz      # the script looks here; elsewhere: SDL2_MINGW=<.../x86_64-w64-mingw32>
   ```
3. Build: `cd engine && tools/package.sh`. It checks the tools first and names anything missing. Other settings:
   `NSIS_ROOT`, `SDL2_ROOT` (unpacked tools instead of installed ones), `BUILD_WIN`, `BUILD_PKG` (build folders),
   `MAINTAINER` (for the deb).
4. Check (optional, with podman or docker): install the package in a clean system and run a test there.
   ```
   cd engine/dist
   podman run --rm -v $PWD:/pkg:ro -v /path/to/SimFarm:/game:ro ubuntu:22.04 bash -c \
     'apt-get update -qq && apt-get install -y -qq /pkg/opensimfarm_*_amd64.deb >/dev/null &&
      SDL_VIDEODRIVER=dummy opensimfarm --selftest /game | tail -1'
   ```
   (add `--security-opt label=disable` on SELinux systems; for the rpm use the `fedora` image and `dnf install -y`).

### The browser version (online play, beta)
The same engine compiled to WebAssembly with Emscripten. The page is `site/play/` (`index.html`, `play.js`,
`play.css`); the build adds `opensimfarm.js` and `opensimfarm.wasm` there (not tracked).

1. The Emscripten SDK, once (no administrator rights needed; 6.0.13 is the version the site is built with):
   ```
   git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
   ~/tools/emsdk/emsdk install 6.0.13
   ~/tools/emsdk/emsdk activate 6.0.13
   ```
2. Build: `engine/tools/build_web.sh` (from the repository's top folder). It finds the SDK in `~/tools/emsdk`, or
   in `$EMSDK`. It builds `engine/build-web/` (`BUILD_WEB` picks another folder) and copies the two files into
   `site/play/`. The first build also downloads Emscripten's SDL2.
3. Play it locally: `python3 -m http.server 8000 --directory site`, then open http://localhost:8000/play/ .
   (Opening the file directly does not work: the browser needs it served.)
4. Test it: see [The browser version, headless](#the-browser-version-headless).

Publishing: `.github/workflows/pages.yml` installs the same SDK, runs `build_web.sh` and publishes `site/` to
GitHub Pages on every push to `main` that changes `site/`, `engine/src/`, `engine/CMakeLists.txt` or the script
(or by hand from the Actions tab). The page needs no special server headers: the game uses no threads.

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
All of these run headless on Linux: no screen, no sound card. Each needs your SimFarm folder; without one given,
it uses the folder the game remembers (`~/.config/opensimfarm/gamepath.txt`).

### The desktop game, headless
The checks are built into the program. `SDL_VIDEODRIVER=dummy` keeps them from opening a window and
`SDL_AUDIODRIVER=dummy` from using the sound card:
```
cd engine
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
./build/opensimfarm --selftest      # every prebuilt farm loaded and saved again: the same bytes, then the signature
./build/opensimfarm --toolstest     # fields, machines, roads, the shop, land, disasters, the crop duster, save and reload
./build/opensimfarm --newgametest   # generated worlds of 96 and 144: a field, a ditch, a harvest sold, land, save and reload
./build/opensimfarm --audiotest     # Modern Sound: every song and effect rendered (about 10 s)
./build/opensimfarm --graphicstest  # Modern Render: smoothing of every tile, and the glides
./build/opensimfarm --langtest      # every built-in language: its letters, its {0} {1} ..., and that its words fit
./build/opensimfarm --simulate 400  # 400 game days, with a report
```
Each prints `ok` lines (or `round-trip OK` for `--selftest`) and exits with 0 when everything passed. A folder can be
given after the option, e.g. `--selftest /path/to/SimFarm` (for `--simulate`: `--simulate DAYS SAVE FOLDER`,
the save defaulting to `SCEN0.SSM`).

### The browser version, headless
`engine/tools/webtest.py` tests the page in a headless Firefox (it needs only Python 3 and Firefox; build first
with `engine/tools/build_web.sh`):
```
python3 engine/tools/webtest.py [--game /path/to/SimFarm] [--out SCREENSHOT_DIR] [--keep]
```
It serves `site/` on a free local port, starts Firefox headless with a new, throw-away profile and drives it through
Marionette, Firefox's built-in remote control. A script cannot use the folder dialog, so it drops your SimFarm folder
onto the page the way a browser does for a dragged folder. It checks the page and the beta notice; that an unusable
folder is refused with a reason; that the game starts, runs, plays sound, and reacts to keys; the Hide bar button;
resizing; uploading a save; that the files and saves are remembered after a reload; and Forget. It prints one
PASS/FAIL line per check, saves screenshots and exits with 0 when everything passed (about 30 s). Nothing is written
to your SimFarm folder or your own Firefox profile. `--keep` leaves Firefox and the server running until Enter.

Chrome, Edge and Safari are not covered by it; try those by hand (step 3 above).

### The desktop game on a hidden screen
To click through the real window and take screenshots without a screen, `engine/tools/play.sh` runs the game on
the hidden X display `:78`. It needs Xvfb, xdotool and ffmpeg (Fedora: `xorg-x11-server-Xvfb xdotool ffmpeg`;
Debian/Ubuntu: `xvfb xdotool ffmpeg`):
```
engine/tools/play.sh start          # starts Xvfb :78 if needed, then the game; the intro is clicked away
engine/tools/play.sh c 320 240      # left click at a point of the 640x480 interface (rc = right click)
engine/tools/play.sh key space      # xdotool keys
engine/tools/play.sh shot NAME      # screenshot -> $OUT/NAME.png (set OUT to a folder of your choice)
engine/tools/play.sh stop
```
It uses your own settings (`~/.config/opensimfarm/`); the coordinates assume `scale=2`.

## Languages
The game starts in your system's language when OpenSimFarm has it, else in English; the Language button under the
region map changes it, and the choice is remembered (`language=` in `settings.txt`).

Each language is a dictionary in the gettext `.po` format in `engine/lang/`. Each entry pairs a text of the game
(`msgid`, in English) with its translation (`msgstr`):
* `engine/lang/opensimfarm.pot` lists every text to translate. It is made from the sources by
  `python3 engine/tools/lang_extract.py`.
* To add a language, copy it to `engine/lang/<code>.po`, for example `es.po`, `fr.po` or `de.po`. Fill in the
  header's `Language` and `X-Language-Name` (the language's name in its own words) and every `msgstr`, then build.
  `{0}`, `{1}` ... stand for numbers and names the game fills in; a translation may put them in another order.
  `\n` starts a new line.
* A `.po` file can also go into the `languages` folder of the settings folder (`~/.config/opensimfarm/languages/` on
  Linux). The game reads it at start, without a rebuild. A file there with the code of a built-in language replaces
  that language.
* Some texts come from your SimFarm files, not from the program: message boxes, item and crop descriptions, the Farm
  Expert's cards and crop names. They are translated by id (`event:12`, `item:40`, `card:30:1`,
  `crophelp:corn.crp`, `crop:corn.crp`); the comment at the top of `engine/tools/lang_extract.py` explains each kind. Keys such as `button:CLOSE` or `gauge:Pests` are
  words printed over a small place in a picture: keep them short.
* Letters: the game's font has only plain letters, so OpenSimFarm adds the accented ones of Latin-1 (`á é í ó ú â ê ô
  ã õ ç à ü ñ` and their capitals, `¡ ¿ ° ª º « »`). Other alphabets cannot be shown yet.

When the program's texts change, `python3 engine/tools/lang_extract.py --update` adds the new texts to every `.po` file
(empty) and moves the ones no longer used to the end. `--check` lists what is missing in each language. After a
build, `./build/opensimfarm --langtest` checks that every translation uses letters the font has, keeps the `{0}` of
its text, and fits the place it is drawn in.

## Licence
GNU General Public License version 3; see [engine/LICENSE.txt](engine/LICENSE.txt).

SimFarm is a trademark of its respective owner. This project is not affiliated with or endorsed by
the owners of SimFarm.
