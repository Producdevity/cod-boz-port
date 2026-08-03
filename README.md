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

Local Wi-Fi works without a server. To use Play Online, edit `config.txt` in
the port directory:

```text
multiplayer_server=server.example.org (or IPv4)
multiplayer_proxy=0
voice_chat=0
```

All players must use the same server address. Cross-play with the PS Vita port
also works when both versions use `boz-online.xubi.org`. Keep
`multiplayer_proxy=0`, this is only relevant during development.

The online server lives in the [`cod-boz-online`](https://github.com/Producdevity/cod-boz-online)
repository and includes instructions for self-hosting.
(I am probably not keeping this server up forever)

Voice chat defaults to off. Set `voice_chat=1` to enable it. The devices I use
during development do not expose ALSA capture device, so voice input on those
systems requires a supported USB microphone or audio adapter.
If you got a fancy linux-arm handheld, this should work out of the box.

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
