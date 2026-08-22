#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

platform="${1:-}"
if [ -z "$platform" ]; then
  echo "Usage: $0 <muos|knulli|darkos|rocknix> [--root PATH] <ssh-host>" >&2
  exit 2
fi
shift

case "$platform" in
  muos)
    target_root="${CODBOZ_DEPLOY_ROOT:-/mnt/mmc}"
    host="${CODBOZ_DEPLOY_HOST:-}"
    port_directory="ROMS/Ports"
    ;;
  knulli)
    target_root="${CODBOZ_KNULLI_DEPLOY_ROOT:-/userdata/roms}"
    host="${CODBOZ_KNULLI_DEPLOY_HOST:-}"
    port_directory="ports"
    ;;
  darkos)
    target_root="${CODBOZ_DARKOS_DEPLOY_ROOT:-/roms}"
    host="${CODBOZ_DARKOS_DEPLOY_HOST:-}"
    port_directory="ports"
    ;;
  rocknix)
    target_root="${CODBOZ_ROCKNIX_DEPLOY_ROOT:-/storage/roms}"
    host="${CODBOZ_ROCKNIX_DEPLOY_HOST:-}"
    port_directory="ports"
    ;;
  *)
    echo "Unknown platform: $platform" >&2
    exit 2
    ;;
esac

usage() {
  case "$platform" in
    muos)
      echo "Usage: $0 muos [--mmc|--sdcard|--root PATH] <ssh-host>" >&2
      ;;
    knulli)
      echo "Usage: $0 knulli [--userdata|--share|--roms|--root PATH] <ssh-host>" >&2
      ;;
    darkos)
      echo "Usage: $0 darkos [--roms|--roms2|--root PATH] <ssh-host>" >&2
      ;;
    rocknix)
      echo "Usage: $0 rocknix [--storage|--root PATH] <ssh-host>" >&2
      ;;
  esac
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --root)
      shift
      if [ "$#" -eq 0 ]; then
        echo "Missing value for --root" >&2
        usage
        exit 2
      fi
      target_root="${1%/}"
      ;;
    --mmc)
      target_root="/mnt/mmc"
      ;;
    --sdcard)
      target_root="/mnt/sdcard"
      ;;
    --userdata)
      target_root="/userdata/roms"
      ;;
    --share)
      target_root="/media/SHARE/roms"
      ;;
    --roms)
      target_root="/roms"
      ;;
    --roms2)
      target_root="/roms2"
      ;;
    --storage)
      target_root="/storage/roms"
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

if [ -z "$host" ]; then
  usage
  exit 2
fi

package_dir="build/package/ports/codboz"
payload="$package_dir/codboz"
gamedir="$target_root/ports/codboz"
portdir="$target_root/$port_directory"

executables=(
  "$payload/codboz_s3e_loader"
  "$payload/codboz_apk_extract"
  "$payload/codboz_setup"
  "$package_dir/CODBOZ.sh"
)
files=(
  "$payload/config.defaults.txt"
  "$package_dir/gameinfo.xml"
  "$package_dir/cover.png"
  "$package_dir/screenshot.png"
)
for required in "${executables[@]}"; do
  if [ ! -x "$required" ]; then
    echo "Missing executable: $required" >&2
    echo "Run scripts/build-docker.sh first." >&2
    exit 1
  fi
done
for required in "${files[@]}"; do
  if [ ! -f "$required" ]; then
    echo "Missing packaging file: $required" >&2
    echo "Run scripts/build-docker.sh first." >&2
    exit 1
  fi
done
ssh "$host" 'sh -s' -- "$target_root" "$portdir" <<'REMOTE_CHECK'
set -e
root="$1"
portdir="$2"
if [ ! -d "$root" ]; then
  echo "Deploy root does not exist: $root" >&2
  exit 1
fi
if [ ! -d "$portdir" ]; then
  echo "Ports directory does not exist: $portdir" >&2
  exit 1
fi
REMOTE_CHECK

printf 'Deploying COD BOZ to %s:%s\n' "$host" "$gamedir"
ssh "$host" 'sh -s' -- "$gamedir" <<'REMOTE_MKDIR'
set -e
gamedir="$1"
mkdir -p "$gamedir" "$gamedir/apk" "$gamedir/assets" "$gamedir/licenses"
REMOTE_MKDIR

scp "$payload/codboz_s3e_loader" "$host:$gamedir/codboz_s3e_loader.tmp"
scp "$payload/codboz_apk_extract" "$host:$gamedir/codboz_apk_extract.tmp"
scp "$payload/codboz_setup" "$host:$gamedir/codboz_setup.tmp"
scp "$package_dir/CODBOZ.sh" "$host:$portdir/CODBOZ.sh.tmp"
scp "$payload/config.defaults.txt" "$host:$gamedir/config.defaults.txt.tmp"
scp "$package_dir/gameinfo.xml" "$host:$gamedir/gameinfo.xml.tmp"
scp "$package_dir/cover.png" "$host:$gamedir/cover.png.tmp"
scp "$package_dir/screenshot.png" "$host:$gamedir/screenshot.png.tmp"
scp "$payload/licenses/"*.txt "$host:$gamedir/licenses/"

ssh "$host" 'sh -s' -- "$gamedir" "$portdir" <<'REMOTE_INSTALL'
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
mv "$gamedir/config.defaults.txt.tmp" "$gamedir/config.defaults.txt"
mv "$gamedir/gameinfo.xml.tmp" "$gamedir/gameinfo.xml"
mv "$gamedir/cover.png.tmp" "$gamedir/cover.png"
mv "$gamedir/screenshot.png.tmp" "$gamedir/screenshot.png"

chmod 755 "$gamedir/codboz_s3e_loader" "$gamedir/codboz_apk_extract" \
  "$gamedir/codboz_setup" "$portdir/CODBOZ.sh"
chmod 644 "$gamedir/config.defaults.txt" "$gamedir/gameinfo.xml" "$gamedir/cover.png" \
  "$gamedir/screenshot.png" "$gamedir/licenses/"*.txt

if [ ! -f "$gamedir/apk/game.apk" ]; then
  echo "Note: place the Android APK at $gamedir/apk/game.apk for first-launch setup." >&2
fi
REMOTE_INSTALL
