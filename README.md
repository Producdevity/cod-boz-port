# COD BOZ PortMaster Loader

ARMHF loader for the Android Marmalade build of Call of Duty: Black Ops Zombies.

This repository contains only loader/source code and packaging scripts. Game data from the APK is not redistributable and must stay user-provided on the device under `codboz/assets`.

## Layout

- `src/` and `include/`: loader/runtime source.
- `packaging/CODBOZ.sh`: tracked PortMaster launcher script copied to the device.
- `build/`: generated loader output; gitignored.
- `Dockerfile`: pinned ARMHF build environment.
- `scripts/build-docker.sh`: builds `build/codboz_s3e_loader` using Docker and the Makefile.
- `scripts/deploy-muos.sh`: deploys the built loader and launcher to `muos-h` by default.

## Build

```bash
scripts/build-docker.sh
```

## Deploy

This script is for personal use with my setup for now, will make this a generic script later.

```bash
scripts/deploy-muos.sh
```

Use another SSH host by passing it as the first argument:

```bash
scripts/deploy-muos.sh muos-h
```
