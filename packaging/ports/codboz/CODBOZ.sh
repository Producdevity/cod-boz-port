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
export PORT_32BIT="Y"
# shellcheck source=/dev/null
source "$controlfolder/device_info.txt"
# shellcheck source=/dev/null
[ -f "$controlfolder/tasksetter" ] && source "$controlfolder/tasksetter"
# shellcheck source=/dev/null
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

: "${directory:?PortMaster control.txt did not set directory}"

gamedir="/$directory/ports/codboz"
assetdir="$gamedir/assets"
apkdir="$gamedir/apk"
loader="$gamedir/codboz_s3e_loader"
setup_script="$gamedir/codboz_setup.sh"
s3e="$assetdir/boz.s3e.unpacked"
installed="$gamedir/.installed"
savehome="$gamedir/savedata-home"

mkdir -p "$savehome" "$apkdir" "$assetdir"
cd "$gamedir" || exit 1
> "$gamedir/log.txt" && exec > >(tee "$gamedir/log.txt") 2>&1

finish_port() {
  if command -v pm_finish >/dev/null 2>&1; then
    pm_finish
  fi
}

end_splash() {
  if command -v pm_end_splash >/dev/null 2>&1; then
    pm_end_splash
  fi
}

message() {
  echo "$1"
  if command -v pm_message >/dev/null 2>&1; then
    pm_message "$1"
  fi
}

show_error() {
  echo "ERROR: $1 - $2"
  end_splash
  if command -v pm_show_error >/dev/null 2>&1; then
    pm_show_error "$1" "$2"
  else
    message "$1: $2"
    sleep 8
  fi
}

has_game_data() {
  [ -f "$s3e" ] && [ -s "$assetdir/blackops_etc.dz" ] && [ -s "$assetdir/blackops_gles1.dz" ]
}

# shellcheck disable=SC2329
on_signal() {
  [ -n "${game_pid:-}" ] && kill -TERM "$game_pid" 2>/dev/null || true
  end_splash
  finish_port
  exit 130
}
trap on_signal INT TERM HUP

require_file() {
  if [ ! -f "$1" ]; then
    show_error "$2" "$3"
    exit 1
  fi
}

require_executable() {
  if [ ! -x "$1" ]; then
    show_error "$2" "$3"
    exit 1
  fi
}

first_run_setup() {
  require_executable "$setup_script" "Missing Setup Script" "The port install is incomplete: codboz_setup.sh is missing."
  require_file "$controlfolder/utils/patcher.txt" "PortMaster Update Required" "This port needs PortMaster's patcher utility. Update PortMaster, then launch again."

  export PATCHER_FILE="$setup_script"
  export PATCHER_GAME="Call of Duty: Black Ops Zombies"
  export PATCHER_TIME="10-30 minutes"

  # shellcheck source=/dev/null
  source "$controlfolder/utils/patcher.txt"
}

require_executable "$loader" "Missing Loader" "The port install is incomplete: codboz_s3e_loader is missing."

if ! has_game_data; then
  first_run_setup
fi

if ! has_game_data; then
  show_error "Setup Failed" "Check ports/codboz/setup.log, then launch again."
  exit 1
fi

if has_game_data && [ ! -f "$installed" ]; then
  touch "$installed"
fi

require_file "$s3e" "Missing Game Data" "Setup did not create assets/boz.s3e.unpacked."
end_splash

export HOME="$savehome"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}"
export PIPEWIRE_RUNTIME_DIR="${PIPEWIRE_RUNTIME_DIR:-/run}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/dbus/system_bus_socket}"
[ -z "${SPA_PLUGIN_DIR:-}" ] && [ -d /usr/lib32/spa-0.2 ] && export SPA_PLUGIN_DIR=/usr/lib32/spa-0.2
[ -z "${PIPEWIRE_MODULE_DIR:-}" ] && [ -d /usr/lib32/pipewire-0.3 ] && export PIPEWIRE_MODULE_DIR=/usr/lib32/pipewire-0.3
[ -z "${ALSA_CONFIG:-}" ] && [ -f /usr/share/alsa/alsa.conf ] && export ALSA_CONFIG=/usr/share/alsa/alsa.conf
export LD_LIBRARY_PATH="$gamedir/libs.armhf:${LD_LIBRARY_PATH:-}"
if [ -n "${sdl_controllerconfig:-}" ]; then
  export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi

if [ -n "${GPTOKEYB:-}" ]; then
  $GPTOKEYB "codboz_s3e_loader" &
fi
if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$loader"
fi

${TASKSET:-} "$loader" --root "$gamedir" --run "$s3e" &
game_pid=$!
wait "$game_pid"
status=$?
finish_port
exit "$status"
