# uuu-gui

Графический интерфейс для [NXP UUU](https://github.com/nxp-imx/mfgtools) (Universal Update Utility) — инструмента прошивки iMX-устройств по USB.

![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)

## Возможности

- Управление пресетами прошивки (SimpleBin, EmmcAll, EmmcAll4G)
- Автоматическое определение подключённых NXP-устройств
- Одновременная прошивка нескольких устройств
- Прогресс-бар с масштабированием по фазам
- Авто-прошивка при подключении устройства
- Поддержка sudo / pkexec (Linux)
- Интерфейс на русском и английском языках
- Лог вывода uuu для каждого устройства

## Требования

- [uuu](https://github.com/nxp-imx/mfgtools/releases) — бинарник нужно скачать отдельно и указать путь в Settings
- Qt 6.4+
- libusb 1.0

## Сборка

### Linux

```bash
sudo apt install qt6-base-dev libqt6svg6-dev libusb-1.0-0-dev cmake build-essential

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/uuuapp
```

Для работы с USB без sudo добавить udev-правило:

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1fc9", MODE="0666"' | sudo tee /etc/udev/rules.d/70-nxp.rules
sudo udevadm control --reload-rules
```

Без udev-правила потребуется pkexec или sudo — приложение спросит само при необходимости.

### macOS

```bash
brew install qt libusb

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libusb)"
cmake --build build

open build/uuuapp.app
```

**Первый запуск:** приложение не подписано Apple-сертификатом, поэтому при первом запуске нужно выполнить:

```bash
xattr -cr /path/to/uuuapp.app
```

Или: правой кнопкой → Открыть → Открыть в диалоге.

### Windows

Зависимости через [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
vcpkg install libusb --triplet x64-windows
```

Установить [Qt 6](https://www.qt.io/download-qt-installer), затем:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
windeployqt build/Release/uuuapp.exe
```

## Готовые сборки

Бинарники собираются автоматически в GitHub Actions при каждом пуше в `main`.

Скачать можно на вкладке **Actions → последний успешный билд → Artifacts**:

| Платформа | Артефакт |
|-----------|----------|
| Linux | `uuu-gui-linux-appimage` (AppImage), `uuu-gui-linux-deb` (.deb) |
| macOS | `uuu-gui-macos` (DMG) |
| Windows | `uuu-gui-windows` (папка с .exe и DLL) |

## Настройка

При первом запуске открыть **Settings**:

- **UUU Binary** — путь до бинарника `uuu` (скачать с [releases](https://github.com/nxp-imx/mfgtools/releases))
- **Privilege** — пусто (Linux с udev-правилом или macOS/Windows), `sudo` или `pkexec` (Linux без udev)
- **Language** — русский / English

## Пресеты прошивки

| Тип | Описание |
|-----|----------|
| **SimpleBin** | Одна фаза: `uuu <file.bin>` |
| **EmmcAll** | Одна фаза: `uuu -b emmc_all <bootloader> <image.wic>` |
| **EmmcAll4G** | Две фазы: сначала `uuu <4g-init.bin>`, затем `uuu -b emmc_all <bootloader> <image.wic>` |

Пресеты сохраняются между сессиями. При подключении нескольких устройств одновременно Phase 2 (`-b emmc_all`) прошивает все подключённые устройства за один проход — второе устройство ждёт результата и не запускает отдельный процесс uuu.
