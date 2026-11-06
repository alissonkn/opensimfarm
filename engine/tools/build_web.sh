#!/bin/bash
# Builds the browser version of OpenSimFarm (WebAssembly, with Emscripten) and puts it into site/play/, next to its
# page. The GitHub Pages workflow runs this too; the two built files are not tracked.
#
#   engine/tools/build_web.sh            build into engine/build-web and copy to site/play/
#   EMSDK=~/tools/emsdk engine/tools/build_web.sh
#
# Needs the Emscripten SDK (https://emscripten.org, no admin rights needed):
#   git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
#   ~/tools/emsdk/emsdk install 6.0.13 && ~/tools/emsdk/emsdk activate 6.0.13
# SDL2 comes with Emscripten (downloaded on the first build). To try it, serve site/ with any web server, e.g.
#   python3 -m http.server 8000 --directory site      then open http://localhost:8000/play/
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
engine="$(dirname "$here")"
site="$(dirname "$engine")/site"
if ! command -v emcmake >/dev/null 2>&1; then
    EMSDK="${EMSDK:-$HOME/tools/emsdk}"
    if [ -f "$EMSDK/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
    fi
fi
command -v emcmake >/dev/null 2>&1 || { echo "Emscripten not found: install the emsdk (see the top of this script) or set EMSDK" >&2; exit 1; }
build="${BUILD_WEB:-$engine/build-web}"
emcmake cmake -S "$engine" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc 2>/dev/null || echo 4)"
cp "$build/opensimfarm.js" "$build/opensimfarm.wasm" "$site/play/"
ls -l "$site/play/opensimfarm.js" "$site/play/opensimfarm.wasm"
