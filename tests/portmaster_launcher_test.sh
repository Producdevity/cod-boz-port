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

wait_for_text() {
  local file="$1"
  local expected="$2"
  local _
  for _ in {1..50}; do
    if grep -Fq "$expected" "$file" 2>/dev/null; then
      return 0
    fi
    sleep 0.02
  done
  [ ! -f "$file" ] || cat "$file" >&2
  return 1
}

printf 'directory=%q\n' "${storage#/}" > "$control_dir/control.txt"
cat >> "$control_dir/control.txt" <<'CONTROL'
get_controls() { :; }
pm_show_error() { [ -z "${ERROR_RESULT:-}" ] || printf 'error\n' >> "$ERROR_RESULT"; }
pm_finish() { [ -z "${FINISH_RESULT:-}" ] || printf 'finish\n' >> "$FINISH_RESULT"; }
CONTROL

runtime_target="$test_root/runtime-target.sh"
runtime_target_result="$test_root/runtime-target.txt"
cat > "$runtime_target" <<'TARGET'
#!/bin/sh
printf '%s\n' "$@" > "$RUNTIME_TARGET_RESULT"
TARGET
chmod 755 "$runtime_target"

LD_LIBRARY_PATH="" RUNTIME_TARGET_RESULT="$runtime_target_result" \
  bash packaging/ports/codboz/codboz/codboz_runtime.sh "$runtime_target" direct
grep -qx 'direct' "$runtime_target_result"

if [ ! -x /lib/ld-linux-armhf.so.3 ]; then
  fake_lib64="$test_root/lib64"
  fake_lib32="$test_root/lib32"
  runtime_loader_result="$test_root/runtime-loader.txt"
  mkdir -p "$fake_lib64" "$fake_lib32"
  printf '\177ELF\002' > "$fake_lib64/libc.so.6"
  for name in libc.so.6 libEGL.so.1 libGLESv1_CM.so.1 libSDL2-2.0.so.0; do
    printf '\177ELF\001' > "$fake_lib32/$name"
  done
  cat > "$fake_lib32/ld-linux-armhf.so.3" <<'LOADER'
#!/bin/sh
printf '%s\n' "$@" > "$RUNTIME_LOADER_RESULT"
[ "$1" = "--library-path" ]
shift 2
exec "$@"
LOADER
  chmod 755 "$fake_lib32/ld-linux-armhf.so.3"
  ln -s ../lib32/ld-linux-armhf.so.3 "$fake_lib64/ld-linux-armhf.so.3"
  fake_lib32_resolved="$(realpath "$fake_lib32")"

  LD_LIBRARY_PATH="$fake_lib64:$fake_lib32" \
    RUNTIME_LOADER_RESULT="$runtime_loader_result" \
    RUNTIME_TARGET_RESULT="$runtime_target_result" \
    bash packaging/ports/codboz/codboz/codboz_runtime.sh "$runtime_target" fallback
  sed -n '1p' "$runtime_loader_result" | grep -qx -- '--library-path'
  sed -n '2p' "$runtime_loader_result" | grep -qx "$fake_lib32_resolved"
  sed -n '3p' "$runtime_loader_result" | grep -qx "$runtime_target"
  grep -qx 'fallback' "$runtime_target_result"
fi

missing_defaults_output="$test_root/missing-defaults.txt"
if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh \
  >"$missing_defaults_output" 2>&1; then
  echo "launcher unexpectedly succeeded without its configuration defaults" >&2
  exit 1
fi
wait_for_text "$missing_defaults_output" 'config.defaults.txt is missing.'
test ! -e "$game_dir/config.txt"

cp packaging/ports/codboz/codboz/config.defaults.txt "$game_dir/config.defaults.txt"
cp packaging/ports/codboz/codboz/codboz_runtime.sh "$game_dir/codboz_runtime.sh"

if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded without its loader" >&2
  exit 1
fi
cmp "$game_dir/config.defaults.txt" "$game_dir/config.txt"
grep -qx 'multiplayer_server=boz-online.xubi.org' "$game_dir/config.txt"
grep -qx 'multiplayer_proxy=0' "$game_dir/config.txt"
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

mkdir -p "$control_dir/utils"
cat > "$control_dir/utils/patcher.txt" <<'PATCHER'
printf 'patcher\n' > "$PATCHER_RESULT"
PATCHER
cat > "$game_dir/codboz_s3e_loader" <<'LOADER'
#!/bin/sh
exit 0
LOADER
cat > "$game_dir/codboz_setup" <<'SETUP'
#!/bin/sh
exit 0
SETUP
chmod 755 "$game_dir/codboz_s3e_loader" "$game_dir/codboz_setup"

patcher_result="$test_root/patcher.txt"
error_result="$test_root/error.txt"
finish_result="$test_root/finish.txt"
if XDG_DATA_HOME="$xdg_data" \
  PATCHER_RESULT="$patcher_result" \
  ERROR_RESULT="$error_result" \
  FINISH_RESULT="$finish_result" \
  bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded after setup produced no game data" >&2
  exit 1
fi
grep -qx 'patcher' "$patcher_result"
test ! -e "$error_result"
grep -qx 'finish' "$finish_result"

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
