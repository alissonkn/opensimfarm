#!/bin/bash
# Plays a game from a pack alone through the interface (tools/packplay.script), with no display and none of SimFarm's files,
# and fails if what the script expects of the game is not so. Usage: tools/packplay_test.sh [path to opensimfarm] [pack folder]
# Needs SDL2 and the program built (see BUILDING.md); SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy are set here.
here="$(cd "$(dirname "$0")" && pwd)"
bin="$(realpath "${1:-$here/../build/opensimfarm}")"
pack="$(realpath "${2:-$here/../packs/placeholder}")"
home="$(mktemp -d)"
trap 'rm -rf "$home"' EXIT
cd "$home" || exit 1
HOME="$home" XDG_CONFIG_HOME="$home/.config" OPENSIMFARM_SCRIPT="$here/packplay.script" OSF_WORLDSEED=7 OSF_SEED=7 \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 "$bin" --pack "$pack" 2>&1 | grep -E "^script:|cannot|Could not"
status=${PIPESTATUS[0]}
if [ "$status" -eq 0 ]; then echo "pack play: ok"; else echo "pack play: FAILED (exit code $status)"; fi
exit "$status"
