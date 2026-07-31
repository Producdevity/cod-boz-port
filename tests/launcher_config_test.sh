#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

test_root="$(mktemp -d /tmp/codboz-launcher-config.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT

xdg_data="$test_root/xdg"
control_dir="$xdg_data/PortMaster"
storage="$test_root/storage"
game_dir="$storage/ports/codboz"
mkdir -p "$control_dir" "$game_dir"

printf 'directory=%q\nget_controls() { :; }\npm_show_error() { :; }\npm_finish() { :; }\n' \
  "${storage#/}" > "$control_dir/control.txt"
printf 'multiplayer_server=\nmultiplayer_proxy=0\nvoice_chat=0\n' \
  > "$game_dir/config.example.txt"

if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded without its loader" >&2
  exit 1
fi
cmp "$game_dir/config.example.txt" "$game_dir/config.txt"

printf 'multiplayer_server=192.168.178.37\nmultiplayer_proxy=0' > "$game_dir/config.txt"
if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded without its loader" >&2
  exit 1
fi
grep -qx 'multiplayer_server=192.168.178.37' "$game_dir/config.txt"
grep -qx 'multiplayer_proxy=0' "$game_dir/config.txt"
test "$(grep -c '^voice_chat=0$' "$game_dir/config.txt")" -eq 1

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
  CFW_NAME=knulli \
  DEVICE_NAME=RG40XX-H \
  CONTROLLER_RESULT="$controller_result" \
  sdl_controllerconfig='custom-controller-mapping' \
  bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1
grep -qx 'custom-controller-mapping' "$controller_result"

echo "launcher config tests passed"
