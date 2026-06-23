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
# shellcheck source=/dev/null
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

: "${directory:?PortMaster control.txt did not set directory}"

gamedir="/$directory/ports/codboz"
assetdir="$gamedir/assets"
apkdir="$gamedir/apk"
extractor="$gamedir/codboz_apk_extract"
apk="$apkdir/game.apk"
s3e="$assetdir/boz.s3e.unpacked"
installed="$gamedir/.installed"
setup_log="$gamedir/setup.log"

mkdir -p "$assetdir" "$apkdir"
: > "$setup_log"
exec > >(tee -a "$setup_log") 2>&1

failure_reported=0

fail() {
  failure_reported=1
  echo "PATCH_FAIL_MSG:$1"
  echo ""
  echo "ERROR: $1"
  echo "Patching process failed!"
  exit 1
}

on_exit() {
  status=$?
  if [ "$status" -ne 0 ] && [ "$failure_reported" -eq 0 ]; then
    echo "PATCH_FAIL_MSG:Setup failed. Check ports/codboz/setup.log."
    echo ""
    echo "ERROR: Setup failed with exit code $status."
    echo "Patching process failed!"
  fi
}
trap on_exit EXIT

require_file() {
  if [ ! -f "$1" ]; then
    fail "$2"
  fi
}

require_executable() {
  if [ ! -x "$1" ]; then
    fail "$2"
  fi
}

file_size() {
  wc -c < "$1" | tr -d ' '
}

has_game_data() {
  [ -f "$s3e" ] && [ -s "$assetdir/blackops_etc.dz" ] && [ -s "$assetdir/blackops_gles1.dz" ]
}

download_file() {
  name="$1"
  url="${cdn_base%/}/$name"
  target="$assetdir/$name"
  tmp="$target.part"

  if [ -s "$target" ]; then
    echo "$name already exists."
    return 0
  fi

  rm -f "$tmp"
  echo "Downloading $name..."
  if command -v wget >/dev/null 2>&1; then
    wget -O "$tmp" "$url" || fail "Could not download $name."
  elif command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 20 -o "$tmp" "$url" || fail "Could not download $name."
  else
    fail "Downloader missing. Install wget or curl, then launch this port again."
  fi

  if [ ! -s "$tmp" ]; then
    rm -f "$tmp"
    fail "Downloaded $name is empty."
  fi

  mv "$tmp" "$target" || fail "Could not store $name."
  echo "Downloaded $name ($(file_size "$target") bytes)."
}

if has_game_data; then
  touch "$installed" || fail "Could not mark setup complete."
  echo "Game data already exists. OK"
  exit 0
fi

require_executable "$extractor" "Missing setup helper: codboz_apk_extract."
require_file "$apk" "Missing APK. Place your Android APK at ports/codboz/apk/game.apk, then launch again."

echo "Extracting game APK..."
"$extractor" extract "$apk" "$assetdir" || fail "Could not extract game.apk."

require_file "$s3e" "boz.s3e.unpacked was not created from game.apk."

cdn_base="$("$extractor" print-cdn "$s3e")" || fail "Could not read the resource CDN from boz.s3e.unpacked."
if [ -z "$cdn_base" ]; then
  fail "Could not find the game resource CDN in game.apk."
fi

echo "Resource CDN found."
for archive in blackops_etc.dz blackops_gles1.dz; do
  download_file "$archive"
done

if ! has_game_data; then
  fail "Setup finished but required game data is still missing."
fi

touch "$installed" || fail "Could not mark setup complete."
echo "Setup complete."
