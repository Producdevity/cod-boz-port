#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

target_root="${CODBOZ_DEPLOY_ROOT:-/mnt/mmc}"
host=""

usage() {
  echo "Usage: $0 [--mmc|--sdcard] <ssh-host>" >&2
  echo "Defaults to /mnt/mmc. Use --sdcard for devices that keep PortMaster on /mnt/sdcard." >&2
  echo "Or set CODBOZ_DEPLOY_HOST and optionally CODBOZ_DEPLOY_ROOT." >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --mmc)
      target_root="/mnt/mmc"
      ;;
    --sdcard)
      target_root="/mnt/sdcard"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      if [ -n "$host" ]; then
        echo "Unexpected argument: $1" >&2
        usage
        exit 2
      fi
      host="$1"
      ;;
  esac
  shift
done

host="${host:-${CODBOZ_DEPLOY_HOST:-}}"
if [ -z "$host" ]; then
  usage
  exit 2
fi

package_dir="build/package/ports/codboz"
port_payload="$package_dir/codboz"
loader="$port_payload/codboz_s3e_loader"
extractor="$port_payload/codboz_apk_extract"
launcher="$package_dir/CODBOZ.sh"
setup_script="$port_payload/codboz_setup.sh"

if [ ! -d "$package_dir" ]; then
  echo "Missing package staging directory: $package_dir" >&2
  echo "Run scripts/build-docker.sh first." >&2
  exit 1
fi

for required in "$loader" "$extractor" "$setup_script"; do
  if [ ! -x "$required" ]; then
    echo "Missing executable: $required" >&2
    echo "Run scripts/build-docker.sh first." >&2
    exit 1
  fi
done

for required in "$launcher" "$package_dir/port.json" "$package_dir/README.md" "$package_dir/gameinfo.xml"; do
  if [ ! -f "$required" ]; then
    echo "Missing packaging file: $required" >&2
    echo "Run scripts/build-docker.sh first." >&2
    exit 1
  fi
done

ssh "$host" 'sh -s' -- "$target_root" <<'REMOTE_CHECK'
set -e
root="$1"
if [ ! -d "$root" ]; then
  echo "Deploy root does not exist: $root" >&2
  exit 1
fi
REMOTE_CHECK

gamedir="$target_root/ports/codboz"
portdir="$target_root/ROMS/Ports"

printf 'Deploying COD BOZ to %s:%s\n' "$host" "$gamedir"
ssh "$host" 'sh -s' -- "$gamedir" "$portdir" <<'REMOTE_MKDIR'
set -e
gamedir="$1"
portdir="$2"
mkdir -p "$gamedir" "$gamedir/apk" "$gamedir/assets" "$portdir"
REMOTE_MKDIR
scp "$loader" "$host:$gamedir/codboz_s3e_loader.tmp"
scp "$extractor" "$host:$gamedir/codboz_apk_extract.tmp"
scp "$setup_script" "$host:$gamedir/codboz_setup.sh.tmp"
scp "$launcher" "$host:$portdir/CODBOZ.sh.tmp"
ssh "$host" 'sh -s' -- "$gamedir" "$portdir" <<'REMOTE'
set -e
gamedir="$1"
portdir="$2"

pids="$(pidof codboz_s3e_loader 2>/dev/null || true)"
[ -n "$pids" ] && kill -TERM $pids 2>/dev/null || true
sleep 0.5
pids="$(pidof codboz_s3e_loader 2>/dev/null || true)"
[ -n "$pids" ] && kill -KILL $pids 2>/dev/null || true

mv "$gamedir/codboz_s3e_loader.tmp" "$gamedir/codboz_s3e_loader"
mv "$gamedir/codboz_apk_extract.tmp" "$gamedir/codboz_apk_extract"
mv "$gamedir/codboz_setup.sh.tmp" "$gamedir/codboz_setup.sh"
mv "$portdir/CODBOZ.sh.tmp" "$portdir/CODBOZ.sh"
chmod 755 "$gamedir/codboz_s3e_loader" "$gamedir/codboz_apk_extract" "$gamedir/codboz_setup.sh" "$portdir/CODBOZ.sh"
ls -l "$gamedir/codboz_s3e_loader" "$gamedir/codboz_apk_extract" "$gamedir/codboz_setup.sh" "$portdir/CODBOZ.sh"

if [ ! -f "$gamedir/apk/game.apk" ]; then
  echo "Note: place the Android APK at $gamedir/apk/game.apk for first-launch setup." >&2
fi
REMOTE
