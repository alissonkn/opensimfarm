#!/bin/bash
# play.sh — drive the OpenSimFarm GUI on the hidden X display :78 (Xvfb) for checks and screenshots.
#   play.sh start [ARGS]   start the engine (no ARGS = region screen, the intro is clicked away)
#   play.sh load SAVE      start the engine with the game folder and a save (SAVE: name in the game folder or a path)
#   play.sh shot NAME      screenshot of the 640x480 interface -> $OUT/NAME.png
#   play.sh c X Y [SLEEP]  left click   ·  rc X Y  right click  ·  key K..  xdotool keys
#   play.sh drag X0 Y0 X1 Y1   press, move, release
#   play.sh stop           close the engine started here (and say if another one is still running)
# Coordinates are the original's 640x480 ones; with scale=2 the window sits at screen 160 + 2x, 119 + 2y.
# BIN=build-dbg play.sh ... uses the checked build. OUT defaults to ../work/png/engine.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-$ROOT/work/png/engine}; mkdir -p "$OUT"
export DISPLAY=:78
pgrep -f 'Xvfb :78' >/dev/null || { Xvfb :78 -screen 0 1600x1200x24 >/dev/null 2>&1 & sleep 1; }
X(){ echo $((160+2*$1)); }; Y(){ echo $((119+2*$1)); }
case $1 in
start) cd "$ROOT/engine"
  setsid env SDL_VIDEODRIVER=x11 ./${BIN:-build}/opensimfarm $2 > "$OUT/play.log" 2>&1 < /dev/null & echo $! > "$OUT/play.pid"; sleep 4
  [ -z "$2" ] && { xdotool key space; sleep 0.3; xdotool key space; sleep 0.5; };;
load) cd "$ROOT/engine"
  setsid env SDL_VIDEODRIVER=x11 ./${BIN:-build}/opensimfarm "$(cat ~/.config/opensimfarm/gamepath.txt)" "$2" > "$OUT/play.log" 2>&1 < /dev/null & echo $! > "$OUT/play.pid"; sleep 4;;
shot) ffmpeg -loglevel error -y -f x11grab -video_size 1280x960 -i :78+160,119 -frames:v 1 -vf scale=640:480:flags=neighbor "$OUT/$2.png";;
c) xdotool mousemove $(X $2) $(Y $3); sleep 0.2; xdotool click 1; sleep ${4:-0.6};;
rc) xdotool mousemove $(X $2) $(Y $3); sleep 0.2; xdotool click 3; sleep 0.5;;
key) shift; xdotool key "$@"; sleep 0.5;;
drag) xdotool mousemove $(X $2) $(Y $3); sleep 0.2; xdotool mousedown 1; sleep 0.2; xdotool mousemove $(X $4) $(Y $5); sleep 0.4; xdotool mousemove $(X $4) $(Y $5); sleep 0.2; xdotool mouseup 1; sleep 0.5;;
stop) [ -f "$OUT/play.pid" ] && kill "$(cat "$OUT/play.pid")" 2>/dev/null; sleep 1
  left=$(pgrep -a -x opensimfarm); [ -n "$left" ] && echo "still running (kill by PID): $left";;
*) sed -n 2,10p "$0";;
esac
