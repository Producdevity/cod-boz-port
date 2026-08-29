#!/bin/bash

if [ "$#" -eq 0 ]; then
  echo "Usage: codboz_runtime.sh PROGRAM [ARGUMENT ...]" >&2
  exit 2
fi

if [ -x /lib/ld-linux-armhf.so.3 ]; then
  exec "$@"
fi

is_elf32() {
  local elf_class
  [ -r "$1" ] || return 1
  elf_class="$(dd if="$1" bs=1 skip=4 count=1 2>/dev/null | od -An -tu1)"
  elf_class="${elf_class//[[:space:]]/}"
  [ "$elf_class" = "1" ]
}

is_complete_runtime() {
  [ -x "$1/ld-linux-armhf.so.3" ] &&
    is_elf32 "$1/libc.so.6" &&
    is_elf32 "$1/libEGL.so.1" &&
    is_elf32 "$1/libGLESv1_CM.so.1" &&
    is_elf32 "$1/libSDL2-2.0.so.0"
}

run_with_runtime() {
  local runtime
  runtime="$(realpath "$1" 2>/dev/null || readlink -f "$1" 2>/dev/null || printf '%s\n' "$1")"
  shift
  export LD_LIBRARY_PATH="$runtime"
  exec "$runtime/ld-linux-armhf.so.3" --library-path "$runtime" "$@"
}

# Spruce mounts this ARMHF root but can fail to expose it at /usr/lib32.
spruce_runtime="/mnt/SDCARD/Persistent/.32bit_chroot/usr/lib"
if is_complete_runtime "$spruce_runtime"; then
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
  run_with_runtime "$spruce_runtime" "$@"
fi

search_path="${LD_LIBRARY_PATH:-}"
while [ -n "$search_path" ]; do
  case "$search_path" in
    *:*)
      library_dir="${search_path%%:*}"
      search_path="${search_path#*:}"
      ;;
    *)
      library_dir="$search_path"
      search_path=""
      ;;
  esac
  if [ -n "$library_dir" ] && is_complete_runtime "$library_dir"; then
    run_with_runtime "$library_dir" "$@"
  fi
done

exec "$@"
