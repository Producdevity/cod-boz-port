#!/bin/bash

run_with_runtime() {
  local runtime
  runtime="$(realpath "$1" 2>/dev/null || readlink -f "$1" 2>/dev/null || printf '%s\n' "$1")"
  shift
  export LD_LIBRARY_PATH="$runtime"
  exec "$runtime/ld-linux-armhf.so.3" --library-path "$runtime" "$@"
}

main() {
  local spruce_runtime

  if [ "$#" -eq 0 ]; then
    echo "Usage: codboz_runtime.sh PROGRAM [ARGUMENT ...]" >&2
    exit 2
  fi

  if [ -x /lib/ld-linux-armhf.so.3 ]; then
    exec "$@"
  fi

  # SpruceOS mounts the Miyoo Flip ARMHF compatibility root here.
  spruce_runtime="/mnt/SDCARD/Persistent/.32bit_chroot/usr/lib"
  if [ -x "$spruce_runtime/ld-linux-armhf.so.3" ]; then
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
    run_with_runtime "$spruce_runtime" "$@"
  fi

  exec "$@"
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  main "$@"
fi
