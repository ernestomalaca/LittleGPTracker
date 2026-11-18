#!/bin/bash
# Compila Chancho 6 para la RG353V y arma el zip para la tarjeta.
# Necesita el kit en /opt/rg35xx-plus-aarch64-SDL2-SDK-0.1.0 (ver
# projects/README.md, "RG35XXPLUS").
set -e
cd "$(git rev-parse --show-toplevel)/projects"
make PLATFORM=RG353V -j8
SDK=/opt/rg35xx-plus-aarch64-SDL2-SDK-0.1.0
OUT=$(mktemp -d)
cp -a resources/RG353V/. "$OUT/"
rm -f "$OUT/empaquetar.sh"
"$SDK/bin/aarch64-none-linux-gnu-strip" -o "$OUT/chancho6/chancho6-rg353v.elf" chancho6-rg353v.elf
cp "$OUT/chancho6/chancho6-rg353v.elf" ./chancho6-rg353v-stripped.elf
(cd "$OUT" && zip -9qr - .) > chancho6-rg353v.zip
rm -rf "$OUT"
echo "listo: projects/chancho6-rg353v.zip y projects/chancho6-rg353v-stripped.elf"
