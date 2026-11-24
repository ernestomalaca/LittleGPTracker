#!/bin/bash
# Compila Chancho 6 para la RG35XX Plus (ALPHA) y arma el zip para la tarjeta.
# Necesita el kit en /opt/rg35xx-plus-aarch64-SDL2-SDK-0.1.0.
# DEMO=carpeta de un proyecto para incluir como lgpt_chancho_try (opcional)
# MANUAL=pdf del manual (opcional)
set -e
cd "$(git rev-parse --show-toplevel)/projects"
make PLATFORM=RG35XXPLUS -j8
SDK=/opt/rg35xx-plus-aarch64-SDL2-SDK-0.1.0
OUT=$(mktemp -d)
cp -a resources/RG35XXPLUS/. "$OUT/"
mkdir -p "$OUT/chancho6/temas" && cp resources/temas/*.xml "$OUT/chancho6/temas/"
rm -f "$OUT/empaquetar.sh"
PYO=$(mktemp -d)
(cd "$PYO" && npm pack -q pyodide@0.26.4 >/dev/null && tar xzf pyodide-0.26.4.tgz)
mkdir -p "$OUT/chancho6/web/pyodide"
for f in pyodide.js pyodide.asm.js pyodide.asm.wasm python_stdlib.zip pyodide-lock.json; do
    cp "$PYO/package/$f" "$OUT/chancho6/web/pyodide/"
done
rm -rf "$PYO"
"$SDK/bin/aarch64-none-linux-gnu-strip" -o "$OUT/chancho6/chancho6-rg35xxplus.elf" chancho6-rg35xxplus.elf
if [ -n "$DEMO" ] && [ -d "$DEMO" ]; then cp -a "$DEMO" "$OUT/chancho6/lgpt_chancho_try"; fi
if [ -n "$MANUAL" ] && [ -f "$MANUAL" ]; then cp "$MANUAL" "$OUT/"; fi
(cd "$OUT" && zip -9qr - .) > chancho6-rg35xxplus-alpha.zip
rm -rf "$OUT"
echo "listo: projects/chancho6-rg35xxplus-alpha.zip"
