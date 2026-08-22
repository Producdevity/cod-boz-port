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
[ -f "$controlfolder/tasksetter" ] && source "$controlfolder/tasksetter"
# shellcheck source=/dev/null
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

: "${directory:?PortMaster control.txt did not set directory}"

gamedir="/$directory/ports/codboz"
assetdir="$gamedir/assets"
apkdir="$gamedir/apk"
loader="$gamedir/codboz_s3e_loader"
setup_script="$gamedir/codboz_setup"
s3e="$assetdir/boz.s3e.unpacked"
savehome="$gamedir/savedata-home"
config="$gamedir/config.txt"
config_defaults="$gamedir/config.defaults.txt"

mkdir -p "$savehome" "$apkdir" "$assetdir"
cd "$gamedir" || exit 1
: > "$gamedir/log.txt" && exec > >(tee "$gamedir/log.txt") 2>&1

show_error() {
  echo "ERROR: $1 - $2"
  if command -v pm_show_error >/dev/null 2>&1; then
    pm_show_error "$1: $2"
  else
    pm_message "$1: $2"
    sleep 8
  fi
}

# PortMaster overwrites packaged files during updates, so create the editable config once.
if [ ! -f "$config" ]; then
  if [ ! -f "$config_defaults" ]; then
    show_error "Missing Configuration Defaults" "The port install is incomplete: config.defaults.txt is missing."
    exit 1
  fi
  config_tmp="$(mktemp "$gamedir/config.txt.XXXXXX")" || {
    show_error "Configuration Initialization Failed" "Could not create a temporary config file."
    exit 1
  }
  trap 'rm -f "$config_tmp"' EXIT
  trap 'exit 1' HUP INT TERM
  if ! cp "$config_defaults" "$config_tmp" || ! chmod 644 "$config_tmp"; then
    show_error "Configuration Initialization Failed" "Could not create config.txt from config.defaults.txt."
    exit 1
  fi
  if ! ln "$config_tmp" "$config" 2>/dev/null && [ ! -f "$config" ]; then
    show_error "Configuration Initialization Failed" "Could not install config.txt."
    exit 1
  fi
  if ! rm -f "$config_tmp"; then
    show_error "Configuration Initialization Failed" "Could not remove the temporary config file."
    exit 1
  fi
  config_tmp=
  trap - EXIT HUP INT TERM
fi

has_game_data() {
  [ -s "$s3e" ] && [ -s "$assetdir/blackops_etc.dz" ] && [ -s "$assetdir/blackops_gles1.dz" ]
}

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
  require_executable "$setup_script" "Missing Setup Script" "The port install is incomplete: codboz_setup is missing."
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

require_file "$s3e" "Missing Game Data" "Setup did not create assets/boz.s3e.unpacked."

export HOME="$savehome"

if [ -S /var/run/pipewire-0 ]; then
  export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
  export PIPEWIRE_RUNTIME_DIR="${PIPEWIRE_RUNTIME_DIR:-/var/run}"
fi

export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-}"

if [ -n "${GPTOKEYB:-}" ]; then
  $GPTOKEYB "codboz_s3e_loader" &
fi
if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$loader"
fi

${TASKSET:-} "$loader" \
  --root "$gamedir" \
  --display-size "${DISPLAY_WIDTH}x${DISPLAY_HEIGHT}" \
  --run "$s3e" &
game_pid=$!
wait "$game_pid"
status=$?
pm_finish
exit "$status"
