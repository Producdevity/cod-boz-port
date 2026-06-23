#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

host="${1:-${CODBOZ_DEPLOY_HOST:-}}"
if [ -z "$host" ]; then
  echo "Usage: $0 <ssh-host>" >&2
  echo "Or set CODBOZ_DEPLOY_HOST." >&2
  exit 2
fi

loader="build/codboz_s3e_loader"
extractor="build/codboz_apk_extract"
launcher="packaging/CODBOZ.sh"
setup_script="packaging/codboz_setup.sh"

for required in "$loader" "$extractor"; do
  if [ ! -x "$required" ]; then
    echo "Missing binary: $required" >&2
    echo "Run scripts/build-docker.sh first." >&2
    exit 1
  fi
done

for required in "$launcher" "$setup_script"; do
  if [ ! -f "$required" ]; then
    echo "Missing packaging file: $required" >&2
    exit 1
  fi
done

remote_root="$(ssh "$host" '
set -e
for root in /mnt/sdcard /mnt/mmc /mnt/union; do
  if [ -d "$root/ROMS/Ports" ]; then
    printf "%s\n" "$root"
    exit 0
  fi
done
echo "Could not find a PortMaster ROMS/Ports directory under /mnt/sdcard, /mnt/mmc, or /mnt/union." >&2
exit 1
')"

gamedir="$remote_root/ports/codboz"
portdir="$remote_root/ROMS/Ports"

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
