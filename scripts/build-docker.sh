#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

image="${CODBOZ_BUILD_IMAGE:-codboz-armhf-builder:debian-buster}"
flags="-O2 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Iinclude -Ithird_party/lzma -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"

docker build -t "$image" .
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$PWD":/src \
  -w /src \
  "$image" \
  make clean all CC=arm-linux-gnueabihf-gcc CFLAGS="$flags"

file build/codboz_s3e_loader build/codboz_apk_extract
