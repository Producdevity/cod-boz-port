# COD BOZ PortMaster Loader

ARMHF loader for the Android Marmalade build of Call of Duty: Black Ops Zombies.

This repository contains only loader/source code and packaging scripts. Game data from the APK is not redistributable and must stay user-provided on the device under `codboz/assets`.

## Layout

- `src/`: loader and runtime source.
- `include/`: runtime headers.
- `packaging/`: PortMaster launcher files.
- `scripts/`: build and deployment helpers.
- `build/`: generated output; gitignored.
- `Dockerfile`: ARMHF build environment.

## Build

```bash
scripts/build-docker.sh
```

## Deploy

After building, copy the loader and launcher to a muOS/PortMaster device reachable over SSH:

```bash
scripts/deploy-muos.sh <ssh-host>
```

The host can also be provided with `CODBOZ_DEPLOY_HOST`.
