#!/bin/sh
# Regenerate the committed icon binaries from the source SVGs.
#
#   uuuapp.svg        full-bleed icon  -> Windows .ico, Linux PNG, in-app window icon
#   uuuapp_macos.svg  safe-area variant (rounded tile on a transparent 1024 canvas,
#                      ~80.5% per Apple's macOS icon grid) -> macOS .icns
#
# Requires: rsvg-convert (librsvg), iconutil (macOS), magick (ImageMagick).
# Run from the repo root:  sh icons/generate.sh
set -e
cd "$(dirname "$0")"

# --- macOS .icns (from the safe-area variant) ---
rm -rf uuuapp.iconset && mkdir -p uuuapp.iconset
gen() { rsvg-convert -w "$1" -h "$1" uuuapp_macos.svg -o "uuuapp.iconset/$2"; }
gen 16  icon_16x16.png;    gen 32   icon_16x16@2x.png
gen 32  icon_32x32.png;    gen 64   icon_32x32@2x.png
gen 128 icon_128x128.png;  gen 256  icon_128x128@2x.png
gen 256 icon_256x256.png;  gen 512  icon_256x256@2x.png
gen 512 icon_512x512.png;  gen 1024 icon_512x512@2x.png
iconutil -c icns uuuapp.iconset -o uuuapp.icns
rm -rf uuuapp.iconset
echo "wrote uuuapp.icns"

# --- Windows .ico (multi-size, from the full-bleed icon) ---
rsvg-convert -w 1024 -h 1024 uuuapp.svg -o /tmp/uuuapp_ico_src.png
magick /tmp/uuuapp_ico_src.png -define icon:auto-resize=256,128,64,48,32,16 uuuapp.ico
rm -f /tmp/uuuapp_ico_src.png
echo "wrote uuuapp.ico"
