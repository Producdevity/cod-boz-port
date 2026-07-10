#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

image="${CODBOZ_BUILD_IMAGE:-codboz-armhf-builder:debian-buster}"

docker build -t "$image" .
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$PWD":/src \
  -w /src \
  "$image" \
  make clean zip \
    CC=arm-linux-gnueabihf-gcc \
    CFLAGS=-O2 \
    TARGET_CFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"

file build/codboz_s3e_loader build/codboz_apk_extract
