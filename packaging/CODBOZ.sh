#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

# shellcheck source=/dev/null
source "$controlfolder/control.txt"
# shellcheck source=/dev/null
source "$controlfolder/device_info.txt"
# shellcheck source=/dev/null
source "$controlfolder/tasksetter"
# shellcheck source=/dev/null
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

: "${directory:?PortMaster control.txt did not set directory}"

gamedir="/$directory/ports/codboz"
assetdir="$gamedir/assets"
loader="$gamedir/codboz_s3e_loader"
s3e="$assetdir/boz.s3e.unpacked"
savehome="$gamedir/savedata-home"

mkdir -p "$gamedir/logs" "$savehome"
: > "$gamedir/log.txt"
exec >> "$gamedir/log.txt" 2>&1

finish_port() {
  if command -v pm_finish >/dev/null 2>&1; then
    pm_finish
  fi
}

on_signal() {
  [ -n "${game_pid:-}" ] && kill -TERM "$game_pid" 2>/dev/null || true
  finish_port
  exit 130
}
trap on_signal INT TERM HUP

if [ ! -x "$loader" ]; then
  echo "Missing loader: $loader"
  sleep 5
  exit 1
fi
if [ ! -f "$s3e" ]; then
  echo "Missing S3E image: $s3e"
  sleep 5
  exit 1
fi

export HOME="$savehome"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}"
export PIPEWIRE_RUNTIME_DIR="${PIPEWIRE_RUNTIME_DIR:-/run}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/dbus/system_bus_socket}"
export LD_LIBRARY_PATH="$gamedir/lib:/usr/lib32:/lib32:${LD_LIBRARY_PATH:-}"
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-${sdl_controllerconfig:-}}"
unset SDL_GAMECONTROLLERCONFIG_FILE

if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$loader"
fi

cd "$gamedir"
${TASKSET:-} "$loader" --root "$gamedir" --run "$s3e" &
game_pid=$!
wait "$game_pid"
status=$?
finish_port
exit "$status"
