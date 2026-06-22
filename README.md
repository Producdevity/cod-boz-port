# COD BOZ PortMaster Loader

ARMHF loader for the Android Marmalade build of Call of Duty: Black Ops Zombies.

This repository contains only loader/source code and packaging scripts.

# Game Data

You must provide the game data in the `codboz/assets` directory on the device.
The game data is not redistributable and must stay user-provided on the device under `codboz/assets`.

The latest official version of this `apk` is `1.0.11` and its SHA-256 is `359ee68b6e0a3a66e921ec9b955b290dedb93135fd3c20904bc1bb6f47b5499d`.
This port only works with that exact version.

## Layout

- `src/`: loader and runtime source.
- `include/`: runtime headers.
- `packaging/`: PortMaster launcher files.
- `scripts/`: build and deployment helpers.
- `build/`: generated output.
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

I use an Anbernic RG35XX-H running muOS during development, but any device running firmware supported by PortMaster should work.
