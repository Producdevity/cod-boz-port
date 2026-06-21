#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

host="${1:-muos-h}"
loader="build/codboz_s3e_loader"
launcher="packaging/CODBOZ.sh"

if [ ! -x "$loader" ]; then
  echo "Missing loader: $loader" >&2
  echo "Run scripts/build-docker.sh first." >&2
  exit 1
fi

if [ ! -f "$launcher" ]; then
  echo "Missing launcher: $launcher" >&2
  exit 1
fi

ssh "$host" 'test -d /mnt/mmc/ports/codboz && test -d /mnt/mmc/ROMS/Ports'
scp -q "$loader" "$host:/mnt/mmc/ports/codboz/codboz_s3e_loader.tmp"
scp -q "$launcher" "$host:/mnt/mmc/ROMS/Ports/CODBOZ.sh.tmp"
ssh "$host" '
set -e
pids="$(pidof codboz_s3e_loader 2>/dev/null || true)"
[ -n "$pids" ] && kill -TERM $pids 2>/dev/null || true
sleep 0.5
pids="$(pidof codboz_s3e_loader 2>/dev/null || true)"
[ -n "$pids" ] && kill -KILL $pids 2>/dev/null || true
mv /mnt/mmc/ports/codboz/codboz_s3e_loader.tmp /mnt/mmc/ports/codboz/codboz_s3e_loader
mv /mnt/mmc/ROMS/Ports/CODBOZ.sh.tmp /mnt/mmc/ROMS/Ports/CODBOZ.sh
chmod 755 /mnt/mmc/ports/codboz/codboz_s3e_loader /mnt/mmc/ROMS/Ports/CODBOZ.sh
ls -l /mnt/mmc/ports/codboz/codboz_s3e_loader /mnt/mmc/ROMS/Ports/CODBOZ.sh
'
