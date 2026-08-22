## Notes

Thanks to Ideaworks3D and Activision for the original Android release.

This port does not include game data. You need to provide your own legally owned Android APK.

## Installation

Copy the APK to:

```text
ports/codboz/apk/game.apk
```

On first launch, the PortMaster setup screen extracts the required Marmalade files and downloads the game resource archives.

Expected APK SHA-256:

```text
359ee68b6e0a3a66e921ec9b955b290dedb93135fd3c20904bc1bb6f47b5499d
```

## Controls

Press Select to switch between cursor mode and game mode.

| Button      | Action          |
| ----------- | --------------- |
| Left Stick  | Move            |
| Right Stick | Look            |
| D-Pad Down  | Crouch / prone  |
| A           | Action / sprint |
| B           | Reload          |
| X           | Melee           |
| Y           | Grenade         |
| L1 / L2     | Aim             |
| R1 / R2     | Fire            |
| Start       | Pause           |

## Multiplayer

Local Wi-Fi does not use a server. One device hosts and the others join from
the Local Wi-Fi menu.

Play Online uses the settings in `ports/codboz/config.txt`:

```text
multiplayer_server=boz-online.xubi.org
multiplayer_proxy=0
voice_chat=1
player_name=JeKaleVader
```

New installations default to `boz-online.xubi.org`, the server used by the PS
Vita port, so Play Online supports cross-play without additional setup. Existing
`config.txt` files are preserved during updates. Everyone in a match must use the
same server. Leave `multiplayer_proxy=0`. Clear `multiplayer_server` to disable
Play Online. Local Wi-Fi will continue to work.

Voice chat defaults to on so remote players can be heard. The game also attempts
to open a microphone when `voice_chat=1`, but most Linux handhelds do not expose
an ALSA capture device. Voice input on those systems requires a supported USB
microphone or audio adapter. Set `voice_chat=0` to disable both playback and
capture.

The default player name is `JeKaleVader`. `player_name` accepts up to 13 letters,
numbers, spaces, hyphens, underscores or periods.

## Testing

| Distribution | Graphics         | Hardware                | Result                                                            |
| ------------ | ---------------- | ----------------------- | ----------------------------------------------------------------- |
| ROCKNIX      | Wayland / Sway   | Not tested              | Not tested yet                                                    |
| muOS         | fbdev / libmali  | RG35XX H, H700, 640x480 | Current package launches; online and Local Wi-Fi tested           |
| dArkOS       | KMSDRM / libmali | Not tested              | Not tested yet                                                    |
| KNULLI       | KMSDRM / libmali | RG40XX H, H700, 640x480 | Online and Local Wi-Fi previously tested; current package pending |
| ArkOS        | Varies by device | Not tested              | Not tested yet                                                    |
| AmberELEC    | legacy libmali   | Not tested              | Not tested yet                                                    |
