# uuu-gui

> [Русская версия](README.ru.md)

A cross-platform graphical frontend for [NXP UUU](https://github.com/nxp-imx/mfgtools) (Universal Update Utility) — the standard tool for flashing firmware onto NXP i.MX processors via USB.

![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)

## What is this?

NXP i.MX processors (i.MX 8, i.MX 8M, i.MX 6, etc.) are used in embedded Linux boards, industrial computers, and custom hardware. When a board is in recovery mode (SDP — Serial Download Protocol), it exposes itself over USB and can be flashed using NXP's `uuu` command-line tool.

`uuu` is powerful but requires knowing the right command-line arguments, managing multi-phase flashing sequences, and manually tracking which USB path each device is on. When you need to flash multiple boards at once — e.g. in production — this becomes tedious and error-prone.

**uuu-gui** wraps `uuu` in a Qt-based desktop application that:

- Automatically detects NXP devices as they are connected
- Lets you define reusable firmware presets (which files to flash, in what order)
- Flashes multiple devices simultaneously, each in its own `uuu` process
- Handles multi-phase flashing: e.g. loading a 4 GB RAM init binary first, then flashing the full eMMC image
- Automatically detects USB re-enumeration between phases (device changes bus address after SDP boot)
- Shows per-device progress, phase indicators, and live uuu output logs
- Optionally writes timestamped log files for each flash session

The tool does **not** replace `uuu` — it requires a `uuu` binary to be installed separately and calls it under the hood.

## Features

- **Firmware presets** — save and reuse flash configurations (files, type, inter-phase delay)
- **Automatic device detection** — polls `uuu -lsusb` every 500 ms; no manual bus path entry
- **Parallel flashing** — each connected device gets its own `uuu` process with a dedicated `-m busId` flag
- **Multi-phase flashing** — for boards requiring a 4 GB RAM init step before the main eMMC flash
- **USB re-enumeration handling** — after SDP boot the device moves to a new USB address; the app detects the new address by serial number before starting the next phase
- **Auto-flash on connect** — automatically start flashing when a device is plugged in
- **Reboot after flash** — send `FB: reboot` when done
- **Privilege escalation** — `sudo` / `pkexec` support for Linux; `sudo` auto-configured on macOS
- **Log files** — optional timestamped `.log` file per device, written in UTF-8
- **Bilingual UI** — English and Russian, switchable at runtime without restart
- **Dark mode** — automatic on Windows (follows system theme)

## Flash preset types

| Type | What it does |
|------|-------------|
| **SimpleBin** | Single phase: `uuu <file.bin>` — boot or simple SDP flash |
| **EmmcAll** | Single phase: `uuu -b emmc_all <bootloader> <image.wic>` — full eMMC flash |
| **EmmcAll4G** | Two phases: `uuu <4g-init.bin>` then `uuu -b emmc_all <bootloader> <image.wic>` — for boards with 4 GB LPDDR4 that need a special RAM init binary before the main flash |

Presets are stored in application settings and persist across sessions.

## Requirements

- **[uuu](https://github.com/nxp-imx/mfgtools/releases)** — download the binary separately and point the app to it in Settings
- **Qt 6.4+**
- **libusb 1.0** (optional — used as fallback device detection when uuu path is not set)

## Building

### Linux

```bash
sudo apt install qt6-base-dev libqt6svg6-dev libusb-1.0-0-dev cmake build-essential

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/uuuapp
```

To flash USB devices without `sudo`, add a udev rule for NXP devices:

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1fc9", MODE="0666"' | sudo tee /etc/udev/rules.d/70-nxp.rules
sudo udevadm control --reload-rules
```

Without this rule the app will prompt for `pkexec` or `sudo` automatically when a permission error is detected.

### macOS

```bash
brew install qt libusb

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libusb)"
cmake --build build

open build/uuuapp.app
```

On first launch macOS may block the app because it is not signed. To bypass:

```bash
xattr -cr /path/to/uuuapp.app
```

Or: right-click → Open → Open in the dialog.

On macOS, `sudo` is required for libusb to claim NXP USB devices — the app sets this automatically on first run.

### Windows

Install dependencies via [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
vcpkg install libusb --triplet x64-windows
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

## First-time setup

Open **Settings** after launch:

- **UUU Binary** — path to the `uuu` executable ([download from releases](https://github.com/nxp-imx/mfgtools/releases)). The app searches `PATH` and common install locations automatically.
- **Privilege** — leave empty if you have a udev rule (Linux) or are on Windows. Set to `sudo` on macOS (done automatically on first run) or Linux without udev. Use `pkexec` for a graphical password prompt on Linux.
- **Language** — English / Русский
- **Flash Logs** — enable to save a timestamped `.log` file for each flash session to a chosen directory.
