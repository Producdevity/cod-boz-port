#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

target_root="${CODBOZ_KNULLI_DEPLOY_ROOT:-/userdata/roms}"
host=""

usage() {
  echo "Usage: $0 [--userdata|--share|--roms|--root PATH] <ssh-host>" >&2
  echo "Defaults to /userdata/roms, which is KNULLI's active storage path." >&2
  echo "Or set CODBOZ_KNULLI_DEPLOY_HOST and optionally CODBOZ_KNULLI_DEPLOY_ROOT." >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --userdata)
      target_root="/userdata/roms"
      ;;
    --share)
      target_root="/media/SHARE/roms"
      ;;
    --roms)
      target_root="/roms"
      ;;
    --root)
      shift
      if [ "$#" -eq 0 ]; then
        echo "Missing value for --root" >&2
        usage
        exit 2
      fi
      target_root="${1%/}"
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

host="${host:-${CODBOZ_KNULLI_DEPLOY_HOST:-}}"
if [ -z "$host" ]; then
  usage
  exit 2
fi

package_dir="build/package/ports/codboz"
port_payload="$package_dir/codboz"
loader="$port_payload/codboz_s3e_loader"
extractor="$port_payload/codboz_apk_extract"
launcher="$package_dir/CODBOZ.sh"
setup_script="$port_payload/codboz_setup"
gameinfo="$package_dir/gameinfo.xml"
cover="$package_dir/cover.png"
screenshot="$package_dir/screenshot.png"
sdl2_mixer_lib="$port_payload/libs.armhf/libSDL2_mixer-2.0.so.0"

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

for required in "$launcher" "$package_dir/port.json" "$package_dir/README.md" "$gameinfo" "$cover" "$screenshot" "$sdl2_mixer_lib"; do
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
if [ ! -d "$root/ports" ]; then
  echo "Ports directory does not exist: $root/ports" >&2
  exit 1
fi
REMOTE_CHECK

gamedir="$target_root/ports/codboz"
portdir="$target_root/ports"

printf 'Deploying COD BOZ to %s:%s\n' "$host" "$gamedir"
ssh "$host" 'sh -s' -- "$gamedir" "$portdir" <<'REMOTE_MKDIR'
set -e
gamedir="$1"
portdir="$2"
mkdir -p "$gamedir" "$gamedir/apk" "$gamedir/assets" "$portdir"
mkdir -p "$gamedir/libs.armhf" "$gamedir/licenses"
REMOTE_MKDIR

scp "$loader" "$host:$gamedir/codboz_s3e_loader.tmp"
scp "$extractor" "$host:$gamedir/codboz_apk_extract.tmp"
scp "$setup_script" "$host:$gamedir/codboz_setup.tmp"
scp "$launcher" "$host:$portdir/CODBOZ.sh.tmp"
scp "$gameinfo" "$host:$gamedir/gameinfo.xml.tmp"
scp "$cover" "$host:$gamedir/cover.png.tmp"
scp "$screenshot" "$host:$gamedir/screenshot.png.tmp"
scp "$sdl2_mixer_lib" "$host:$gamedir/libs.armhf/libSDL2_mixer-2.0.so.0.tmp"
scp "$port_payload/licenses/"*.txt "$host:$gamedir/licenses/"

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
mv "$gamedir/codboz_setup.tmp" "$gamedir/codboz_setup"
mv "$portdir/CODBOZ.sh.tmp" "$portdir/CODBOZ.sh"
mv "$gamedir/gameinfo.xml.tmp" "$gamedir/gameinfo.xml"
mv "$gamedir/cover.png.tmp" "$gamedir/cover.png"
mv "$gamedir/screenshot.png.tmp" "$gamedir/screenshot.png"
mv "$gamedir/libs.armhf/libSDL2_mixer-2.0.so.0.tmp" "$gamedir/libs.armhf/libSDL2_mixer-2.0.so.0"
chmod 755 "$gamedir/codboz_s3e_loader" "$gamedir/codboz_apk_extract" "$gamedir/codboz_setup" "$portdir/CODBOZ.sh"
chmod 644 "$gamedir/gameinfo.xml" "$gamedir/cover.png" "$gamedir/screenshot.png" "$gamedir/libs.armhf/libSDL2_mixer-2.0.so.0" "$gamedir/licenses/"*.txt
ls -l "$gamedir/codboz_s3e_loader" "$gamedir/codboz_apk_extract" "$gamedir/codboz_setup" "$portdir/CODBOZ.sh"
ls -l "$gamedir/gameinfo.xml" "$gamedir/cover.png" "$gamedir/screenshot.png" "$gamedir/libs.armhf/libSDL2_mixer-2.0.so.0"

if [ ! -f "$gamedir/apk/game.apk" ]; then
  echo "Note: place the Android APK at $gamedir/apk/game.apk for first-launch setup." >&2
fi
REMOTE
