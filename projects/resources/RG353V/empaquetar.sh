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
mkdir -p "$OUT/chancho6/temas" && cp resources/temas/*.xml "$OUT/chancho6/temas/"
mkdir -p "$OUT/chancho6/fuentes" && cp resources/fuentes/* "$OUT/chancho6/fuentes/"
rm -f "$OUT/empaquetar.sh"
# Python para exportar a Live desde chancho.local (sin internet): Pyodide
PYO=$(mktemp -d)
(cd "$PYO" && npm pack -q pyodide@0.26.4 >/dev/null && tar xzf pyodide-0.26.4.tgz)
mkdir -p "$OUT/chancho6/web/pyodide"
for f in pyodide.js pyodide.asm.js pyodide.asm.wasm python_stdlib.zip pyodide-lock.json; do
    cp "$PYO/package/$f" "$OUT/chancho6/web/pyodide/"
done
rm -rf "$PYO"
"$SDK/bin/aarch64-none-linux-gnu-strip" -o "$OUT/chancho6/chancho6-rg353v.elf" chancho6-rg353v.elf
cp "$OUT/chancho6/chancho6-rg353v.elf" ./chancho6-rg353v-stripped.elf
# DEMO=carpeta de un proyecto (va como lgpt_chancho_try); MANUAL=pdf (opcionales)
if [ -n "$DEMO" ] && [ -d "$DEMO" ]; then cp -a "$DEMO" "$OUT/chancho6/lgpt_chancho_try"; fi
if [ -n "$MANUAL" ] && [ -f "$MANUAL" ]; then cp "$MANUAL" "$OUT/"; fi
(cd "$OUT" && zip -9qr - .) > chancho6-rg353v.zip
rm -rf "$OUT"
echo "listo: projects/chancho6-rg353v.zip y projects/chancho6-rg353v-stripped.elf"
