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

missing_defaults_output="$test_root/missing-defaults.txt"
if XDG_DATA_HOME="$xdg_data" bash packaging/ports/codboz/CODBOZ.sh \
  >"$missing_defaults_output" 2>&1; then
  echo "launcher unexpectedly succeeded without its configuration defaults" >&2
  exit 1
fi
grep -q 'Missing Configuration Defaults.*config.defaults.txt is missing' "$missing_defaults_output"
test ! -e "$game_dir/config.txt"

cp packaging/ports/codboz/codboz/config.defaults.txt "$game_dir/config.defaults.txt"

failing_bin="$test_root/failing-bin"
mkdir -p "$failing_bin"
cat > "$failing_bin/cp" <<'FAILING_CP'
#!/bin/sh
printf 'partial config\n' > "$2"
exit 1
FAILING_CP
chmod 755 "$failing_bin/cp"
if PATH="$failing_bin:$PATH" XDG_DATA_HOME="$xdg_data" \
  bash packaging/ports/codboz/CODBOZ.sh >/dev/null 2>&1; then
  echo "launcher unexpectedly succeeded after a failed config copy" >&2
  exit 1
fi
test ! -e "$game_dir/config.txt"
if find "$game_dir" -maxdepth 1 -type f -name 'config.txt.*' -print -quit | grep -q .; then
  echo "launcher left a temporary config file after a failed copy" >&2
  exit 1
fi

concurrent_bin="$test_root/concurrent-bin"
copy_barrier="$test_root/copy-barrier"
mkdir -p "$concurrent_bin" "$copy_barrier"
cat > "$concurrent_bin/cp" <<'BARRIER_CP'
#!/bin/sh
/bin/cp "$1" "$2" || exit 1
: > "$CONFIG_COPY_BARRIER/ready.$$"
attempts=0
while [ "$attempts" -lt 500 ]; do
  set -- "$CONFIG_COPY_BARRIER"/ready.*
  if [ "$1" != "$CONFIG_COPY_BARRIER/ready.*" ] && [ "$#" -ge 2 ]; then
    exit 0
  fi
  attempts=$((attempts + 1))
  sleep 0.01
done
exit 1
BARRIER_CP
cat > "$concurrent_bin/ln" <<'UNSUPPORTED_LN'
#!/bin/sh
exit 1
UNSUPPORTED_LN
chmod 755 "$concurrent_bin/cp" "$concurrent_bin/ln"

concurrent_pids=()
for instance in 1 2; do
  PATH="$concurrent_bin:$PATH" \
    CONFIG_COPY_BARRIER="$copy_barrier" \
    XDG_DATA_HOME="$xdg_data" \
    bash packaging/ports/codboz/CODBOZ.sh \
    >"$test_root/concurrent-$instance.txt" 2>&1 &
  concurrent_pids+=("$!")
done
for pid in "${concurrent_pids[@]}"; do
  if wait "$pid"; then
    echo "concurrent launcher unexpectedly succeeded without its loader" >&2
    exit 1
  fi
done
for instance in 1 2; do
  grep -q 'Missing Loader' "$test_root/concurrent-$instance.txt"
  if grep -q 'Configuration Initialization Failed' "$test_root/concurrent-$instance.txt"; then
    echo "concurrent launcher failed to initialize config.txt" >&2
    exit 1
  fi
done
cmp "$game_dir/config.defaults.txt" "$game_dir/config.txt"
if find "$game_dir" -maxdepth 1 -type f -name 'config.txt.*' -print -quit | grep -q .; then
  echo "concurrent launchers left a temporary config file" >&2
  exit 1
fi
test ! -e "$game_dir/config.txt.lock"

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
