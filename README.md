# COD BOZ PortMaster Loader

Work-in-progress ARMHF loader for the Android Marmalade build of Call of Duty: Black Ops Zombies.

This repository contains only loader/source code, a first-launch APK extractor, and packaging scripts. It does not contain redistributable game data.

<img width="4032" height="3024" alt="IMG_2626" src="https://github.com/user-attachments/assets/8aa8945d-daa2-4a0d-8188-a21527c667aa" />

## Game Data

Place your legally owned Android APK on the device at:

```text
ports/codboz/apk/game.apk
```

On first launch, PortMaster's patcher UI runs `ports/codboz/codboz_setup`. The setup extracts the bundled Marmalade payload and music assets into `ports/codboz/assets`, decompresses `boz.s3e` to `boz.s3e.unpacked`, reads the resource CDN from that unpacked payload, and downloads the required `.dz` archives into `ports/codboz/assets`.

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

Host checks:

```sh
make check
make test-host-sanitize
```

## Multiplayer

Local Wi-Fi works without a server. Play Online uses the settings in
`config.txt` in the port directory:

```text
multiplayer_server=boz-online.xubi.org
multiplayer_proxy=0
voice_chat=1
player_name=JeKaleVader
```

New installations default to `boz-online.xubi.org`, the server used by the PS
Vita port, so Play Online supports cross-play without additional setup. Existing
`config.txt` files are preserved during updates. All players in a match must use
the same server address. Keep `multiplayer_proxy=0`; it is only relevant during
development.

A compatible Go server and self-hosting instructions live in the
[`cod-boz-online`](https://github.com/Producdevity/cod-boz-online) repository.

Voice chat defaults to on so remote players can be heard. The game also attempts
to open a microphone when `voice_chat=1`, but most Linux handhelds do not expose
an ALSA capture device. Voice input on those systems requires a supported USB
microphone or audio adapter. Set `voice_chat=0` to disable both playback and
capture.

The default player name is `JeKaleVader`. `player_name` accepts up to 13 letters,
numbers, spaces, hyphens, underscores or periods.

## Deploy

After building, deploy the staged package to a PortMaster device reachable over SSH:

```bash
scripts/deploy-muos.sh <ssh-host>
scripts/deploy-knulli.sh <ssh-host>
scripts/deploy-darkos.sh <ssh-host>
```

The muOS script defaults to `/mnt/mmc` and also supports `--sdcard`.

The KNULLI script defaults to KNULLI's active storage (`/userdata/roms`).
It also supports `--share`, `--roms`, and `--root PATH`.

The dArkOS/ArkOS script defaults to `/roms` and also supports `--roms2` and `--root PATH`.

The deployment scripts install from the staged package tree. Run `scripts/build-docker.sh` first.
