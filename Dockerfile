FROM debian:buster

RUN set -eux; \
    echo "Acquire::Check-Valid-Until false;" > /etc/apt/apt.conf.d/99-no-check-valid; \
    echo "deb http://archive.debian.org/debian buster main" > /etc/apt/sources.list; \
    echo "deb http://archive.debian.org/debian-security buster/updates main" >> /etc/apt/sources.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf \
        libc6-dev-armhf-cross \
        make \
        qemu-user \
        zip; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
