# uuu-gui

> [Русская версия](README.ru.md)

A cross-platform graphical frontend for [NXP UUU](https://github.com/nxp-imx/mfgtools) (Universal Update Utility) — the standard tool for flashing firmware onto NXP i.MX processors via USB.

![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)

## What is this?

NXP i.MX processors (i.MX 8, i.MX 8M, i.MX 6, etc.) are used in embedded Linux boards, industrial computers, and custom hardware. When a board is in recovery mode (SDP — Serial Download Protocol), it exposes itself over USB and can be flashed using NXP's `uuu` command-line tool.

`uuu` is powerful but requires knowing the right command-line arguments, managing multi-phase flashing sequences, and manually tracking which USB path each device is on. When you need to flash multiple boards at once — e.g. in production — this becomes tedious and error-prone.

**uuu-gui** is a Qt desktop application built directly on NXP's `libuuu` that:

- Automatically detects NXP devices as they are connected
- Lets you define reusable firmware presets (which files to flash, in what order)
- Flashes multiple devices simultaneously, each in its own helper process
- Handles multi-phase flashing: e.g. loading a 4 GB RAM init binary first, then flashing the full eMMC image
- Handles USB re-enumeration between phases (device changes bus address after SDP boot)
- Shows per-device progress, phase indicators, and live flash logs
- Optionally writes timestamped log files for each flash session

The tool **embeds** NXP's `libuuu` (built from source) and drives it through a small bundled `uuu-helper` worker — no separate `uuu` binary needs to be installed. Each device is flashed in its own helper process, so multiple boards can be flashed in parallel with independent presets.

## Features

- **Firmware presets** — save and reuse flash configurations (files, type, inter-phase delay)
- **Automatic device detection** — polls `uuu-helper list` (libuuu) every 500 ms; no manual bus path entry
- **Parallel flashing** — each connected device gets its own `uuu-helper` process, filtered to that device's serial
- **Multi-phase flashing** — for boards requiring a 4 GB RAM init step before the main eMMC flash
- **USB re-enumeration handling** — libuuu re-locates the board by serial number after SDP boot, across the re-enumeration
- **Auto-flash on connect** — automatically start flashing when a device is plugged in
- **Reboot after flash** — send `FB: reboot` when done
- **On-demand privileges** — flashing runs unprivileged; if the device can't be accessed, the app asks for your password once per session and re-runs the flash via `sudo` (no privilege selector to configure)
- **Log files** — optional timestamped `.log` file per device, written in UTF-8
- **Bilingual UI** — English and Russian, switchable at runtime without restart

## Flash preset types

| Type | What it does |
|------|-------------|
| **SimpleBin** | Single phase: `uuu <file.bin>` — boot or simple SDP flash |
| **EmmcAll** | Single phase: `uuu -b emmc_all <bootloader> <image.wic>` — full eMMC flash |
| **EmmcAll4G** | Two phases: `uuu <4g-init.bin>` then `uuu -b emmc_all <bootloader> <image.wic>` — for boards with 4 GB LPDDR4 that need a special RAM init binary before the main flash |

Presets are stored in application settings and persist across sessions.

## Requirements

- **Qt 6.4+**
- **libuuu build dependencies** — `libusb 1.0`, `libzstd`, `tinyxml2`, `bzip2`, `zlib`, `openssl` (libuuu is fetched and built from source at configure time)
- A network connection on the first build (CMake `FetchContent` clones [nxp-imx/mfgtools](https://github.com/nxp-imx/mfgtools) at tag `uuu_1.5.243`)

No separate `uuu` binary is required — the engine is compiled in.

## Building

### Linux

```bash
sudo apt install qt6-base-dev libqt6svg6-dev libusb-1.0-0-dev \
  libzstd-dev libtinyxml2-dev libbz2-dev zlib1g-dev libssl-dev \
  cmake build-essential git

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/uuuapp
```

To flash USB devices without `sudo`, add a udev rule for NXP devices:

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1fc9", MODE="0666"' | sudo tee /etc/udev/rules.d/70-nxp.rules
sudo udevadm control --reload-rules
```

Without this rule the app prompts for your password automatically when it hits a permission error and re-runs the flash via `sudo`.

### macOS

```bash
brew install qt libusb zstd tinyxml2 bzip2 openssl@3 pkg-config

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build

open build/uuuapp.app
```

The build script auto-detects Homebrew and adds the keg-only `pkg-config` paths
(zstd, tinyxml2) and `OPENSSL_ROOT_DIR` for you, so only the Qt prefix is required.

On first launch macOS may block the app because it is not signed. To bypass:

```bash
xattr -cr /path/to/uuuapp.app
```

Or: right-click → Open → Open in the dialog.

On macOS, `sudo` is required for libusb to claim NXP USB devices — the app prompts for your password automatically when it hits a permission error and re-runs the flash elevated.

### Windows

Install dependencies via [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
vcpkg install libusb zstd tinyxml2 bzip2 zlib openssl --triplet x64-windows
```

Install [Qt 6](https://www.qt.io/download-qt-installer), then:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
windeployqt build/Release/uuuapp.exe
```

## Pre-built binaries

Binaries are built automatically by GitHub Actions on every push to `main`.

Download from the **Actions** tab → latest successful run → **Artifacts**:

| Platform | Artifact |
|----------|----------|
| Linux | `uuu-gui-linux-appimage` (AppImage), `uuu-gui-linux-deb` (.deb) |
| macOS | `uuu-gui-macos` (DMG) |
| Windows | `uuu-gui-windows` (folder with .exe and DLLs) |

## Usage

1. Define a **firmware preset** (left panel → **Add**): pick a type and the file(s) to flash.
2. Connect an NXP board in recovery / SDP mode — it appears automatically in the device list.
3. Select the preset and click **Flash** on the device row, or **Flash Checked Devices** for several at once.
4. If the device can't be accessed without elevated privileges, the app asks for your password once and re-runs the flash via `sudo` (cached for the rest of the session).

### Settings

Open **Settings** after launch:

- **Language** — English / Русский (applied immediately, no restart)
- **Flash Logs** — enable to save a timestamped `.log` file for each flash session to a chosen directory.

There is no `uuu` binary path or privilege selector to configure — the `libuuu`
engine is built in, and privileges are requested on demand.
