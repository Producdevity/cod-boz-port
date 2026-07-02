# COD BOZ PortMaster Loader

Work-in-progress ARMHF loader for the Android Marmalade build of Call of Duty: Black Ops Zombies.

This repository contains only loader/source code, a first-launch APK extractor, and packaging scripts. It does not contain redistributable game data.

<img width="4032" height="3024" alt="IMG_2626" src="https://github.com/user-attachments/assets/8aa8945d-daa2-4a0d-8188-a21527c667aa" />

## Game Data

Place your legally owned Android APK on the device at:

```text
ports/codboz/apk/game.apk
```

On first launch, PortMaster's patcher UI runs `ports/codboz/codboz_setup.sh`. The setup extracts the bundled Marmalade payload and music assets into `ports/codboz/assets`, decompresses `boz.s3e` to `boz.s3e.unpacked`, reads the resource CDN from that unpacked payload, and downloads the required `.dz` archives into `ports/codboz/assets`.

The expected APK version is `1.0.11` with SHA-256:

```text
359ee68b6e0a3a66e921ec9b955b290dedb93135fd3c20904bc1bb6f47b5499d
```

## Layout

- `src/`: loader and runtime source.
- `include/`: runtime headers.
- `tools/apk_extract/`: first-launch APK extraction helper.
- `third_party/lzma/`: public-domain LZMA SDK decoder used by the helper.
- `packaging/`: PortMaster launcher files.
- `scripts/`: build and deployment helpers.
- `build/`: generated output.
- `Dockerfile`: ARMHF build environment.

## Build

```bash
scripts/build-docker.sh
```

This builds the ARMHF loader, builds the APK extraction helper, and stages a PortMaster package tree under:

```text
build/package/ports/codboz/
```

## Deploy

After building, copy the loader, extractor, and launcher to a muOS/PortMaster device reachable over SSH:

```bash
scripts/deploy-muos.sh <ssh-host>
```

The host can also be provided with `CODBOZ_DEPLOY_HOST`.

The deploy script installs from the staged package tree. Run `scripts/build-docker.sh` first.
