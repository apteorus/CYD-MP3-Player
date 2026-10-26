# ChonkPod: LCDWiki E32R28T (ESP32-32E)

Source port of [CYD-MP3-Player](https://github.com/Garionhk/CYD-MP3-Player),
working from `apteorus/CYD-MP3-Player` commit
`404332e93974f4f70392b613c9f3dfb25e1b89b8`.
Supports the **resistive-touch E32R28T**, not E32N28T.
[Manufacturer reference](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display).
No compilation or hardware testing has been performed for this port.

The original ST7789 default and both original ILI9341 profiles remain available.
The E32R28T profile is selected with
`-DCYD_BOARD=CYD_BOARD_LCDWIKI_E32R28T` in **both C and C++ flags**.
Do not change the global default in `board.h`. The Windows script passes this
flag to both images and synchronizes the canonical header first.

## Hardware behavior

TFT HSPI is 13/12/14, CS15, DC2, reset -1 (shared EN), backlight21 active high.
Touch remains bit-banged: MOSI32/MISO39/CLK25/CS33/IRQ36; SD owns VSPI:
MOSI23/MISO19/CLK18/CS5. RGB LEDs are active low: red22/green16/blue17.
The amplifier shutdown is independently controlled on GPIO4, active low;
local mono DAC remains GPIO26. Boot explicitly disables the amplifier, including
in the uploader. LED parking never touches GPIO4 on this profile.

GPIO34 is named battery ADC, **not LDR**. No battery gauge, voltage conversion,
auto-brightness, or charging behavior is added. Bluetooth is the first-boot
route; existing saved routes remain unchanged. Change Setup → Audio out if an
older saved speaker preference is present. Mono speaker switching remains usable.
The existing stereo MP3 → A2DP path, discovery, saved peers, AVRCP, playlists,
metadata, folders, themes, backgrounds, resume, and optional uploader are retained.
No Bluetooth address or device name is hardcoded.

## Windows prerequisites (run locally)

Install Git for Windows, Arduino CLI, and Python 3 (with the `py` launcher).
Use PowerShell from the repository root. Commands below use the default Arduino
sketchbook path; if you configured another `directories.user`, substitute that
path (including OneDrive Documents setups).

```powershell
arduino-cli config init # only if you do not already have a CLI config
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.10
arduino-cli lib install TFT_eSPI@2.5.43
$Sketchbook = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino'
$Libraries = Join-Path $Sketchbook 'libraries'
New-Item -ItemType Directory -Force $Libraries | Out-Null
# Run these clones only when the destination does not already exist.
git clone --depth 1 --branch v1.8.11 https://github.com/pschatzmann/ESP32-A2DP.git "$Libraries/ESP32-A2DP"
git clone --depth 1 --branch v0.9.4 https://github.com/pschatzmann/arduino-libhelix.git "$Libraries/arduino-libhelix"
py -m pip install esptool==5.1.0
Copy-Item config/User_Setup_LCDWIKI_E32R28T.h.template "$Libraries/TFT_eSPI/User_Setup.h" -Force
```

Back up any custom TFT_eSPI setup before copying. Reapply the template after
library updates; restore the original board's matching template before building
for it. Use one TFT_eSPI installation, not conflicting IDE/CLI library copies.
AnimatedGIF is vendored; no external installation is needed. Pillow/font
creation is optional; see the main README. These dependency versions are pinned
for reproduction, not a claim that this port has passed compilation.

## Compile and flash locally

The script compiles the player by default; `-WithUploader` builds both. It stops
on a failed command. No serial writes occur unless **`-Flash`** is supplied.

```powershell
# Player only: compile, no hardware access.
./tools/flash_windows.ps1
# Both images: compile, no hardware access.
./tools/flash_windows.ps1 -WithUploader
# Determine the board's COM port in Device Manager or:
arduino-cli board list
$ComPort = 'COM5' # replace with your CH340C port; close other serial monitors
# Compile and flash player only, keeping NVS settings:
./tools/flash_windows.ps1 -Flash -Port $ComPort
# Or compile and flash both (recommended once the player works):
./tools/flash_windows.ps1 -WithUploader -Flash -Port $ComPort
arduino-cli monitor --port $ComPort --config baudrate=115200
```

If Python is named `python` instead of `py`, pass `-Python python`. If CLI is not
on PATH, pass `-ArduinoCli 'C:/path/to/arduino-cli.exe'`. Follow your local
PowerShell script policy; the script does not change that policy.

Builds use `esp32:esp32:esp32:PartitionScheme=min_spiffs,UploadSpeed=115200`,
4 MB flash, with separate build/output directories under `app/build/e32r28t/`.
Player is at **0x10000 (ota_0)**; uploader compiled with `CYD_UPLOADER` is written
only at **0x1F0000 (ota_1)**. Each must fit its ~1.9 MB slot: inspect the local
compile size report. CLI uploads the player's bootloader, partition table and
OTA selector; esptool writes only the uploader slot. No full-chip erase or
merged factory-reset image is used. A previously different partition layout
needs review before updating; back up important device settings first.

Player-only is valid: Setup reports an absent uploader rather than booting an
empty slot. An existing uploader is retained when flashing only the player;
update both together to keep board/profile/version consistent.
`source.txt` next to generated binaries records local HEAD and dirty status;
for distributable builds use a clean checkout and retain that record.

## Bring-up and pending checks

The standard `ILI9341_DRIVER`, BGR order, inversion OFF, rotation1 and 40 MHz
are **initial unverified defaults**, not measurements. The upstream original
ILI9341 profile uses `ILI9341_2_DRIVER` and inversion ON; its settings are not
silently reused as proven for E32R28T. If the screen is blank/garbled, try the
alternative init in the E32R28T template (only one driver defined), recopy and
rebuild both images. For complemented colors use Setup → Invert colours; for
red/blue swaps change template `TFT_RGB_ORDER`. Stored panel/inversion/rotation
settings override boot defaults. Keep existing blind panel recovery and touch
recalibration available; no erase is required to adjust settings.

1. Verify PCB identity and revision. Start on USB only with one data cable.
2. FAT32 SD: 3–5 MP3s under `/music`, a nested folder, ID3 title/artist, and an
   L/R stereo test MP3. Check display colors, rotation, brightness, touch
   calibration, SD navigation, metadata, playlists and playback controls.
3. Pair a commercial Bluetooth speaker. Check distinct L/R channels, pause,
   track changes, speaker buttons, saved-device reconnect after power cycling,
   underruns and heap stability. Test MH-M38 later; do not assume mono hardware
   can demonstrate stereo separation.
4. Switch Bluetooth ↔ onboard mono. On E32R28T red should stay independent;
   amp must be muted at Bluetooth boot and after leaving the speaker route.
5. Install uploader after player bring-up. Test WiFi uploads, passcode, Done,
   reset/power-cycle return to player, and retained settings.
6. Compile and bench-check original profiles with their original templates
   before treating this as regression-tested. Battery work is deferred.

Retain Apache-2.0 license and upstream attribution, plus dependency/vendor
notices, when redistributing. This is a derived ChonkPod build with suffix
`1.2.0-chonkpod-e32r28t`, not an official upstream release.
