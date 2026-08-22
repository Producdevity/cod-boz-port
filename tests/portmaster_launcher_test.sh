#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

test_root="$(mktemp -d /tmp/codboz-portmaster-launcher.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT

xdg_data="$test_root/xdg"
control_dir="$xdg_data/PortMaster"
storage="$test_root/storage"
game_dir="$storage/ports/codboz"
mkdir -p "$control_dir" "$game_dir"

printf 'directory=%q\nget_controls() { :; }\npm_show_error() { :; }\npm_finish() { :; }\n' \
  "${storage#/}" > "$control_dir/control.txt"

if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded without its configuration defaults" >&2
  exit 1
fi
test ! -e "$game_dir/config.txt"

cp packaging/ports/codboz/codboz/config.defaults.txt "$game_dir/config.defaults.txt"

if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded without its loader" >&2
  exit 1
fi
cmp "$game_dir/config.defaults.txt" "$game_dir/config.txt"
grep -qx 'multiplayer_server=boz-online.xubi.org' "$game_dir/config.txt"
grep -qx 'voice_chat=1' "$game_dir/config.txt"
grep -qx 'player_name=JeKaleVader' "$game_dir/config.txt"

existing_config="$test_root/existing-config.txt"
printf 'multiplayer_server=192.168.178.37\nmultiplayer_proxy=0' > "$existing_config"
cp "$existing_config" "$game_dir/config.txt"
if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded without its loader" >&2
  exit 1
fi
cmp "$existing_config" "$game_dir/config.txt"

printf 'game-data\n' > "$game_dir/assets/boz.s3e.unpacked"
printf 'game-data\n' > "$game_dir/assets/blackops_etc.dz"
printf 'game-data\n' > "$game_dir/assets/blackops_gles1.dz"
cat > "$game_dir/codboz_s3e_loader" <<'LOADER'
#!/bin/sh
printf '%s\n' "$SDL_GAMECONTROLLERCONFIG" > "$CONTROLLER_RESULT"
LOADER
chmod 755 "$game_dir/codboz_s3e_loader"

controller_result="$test_root/controller.txt"
XDG_DATA_HOME="$xdg_data" \
  CONTROLLER_RESULT="$controller_result" \
  sdl_controllerconfig='custom-controller-mapping' \
  bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1
grep -qx 'custom-controller-mapping' "$controller_result"

echo "PortMaster launcher tests passed"
