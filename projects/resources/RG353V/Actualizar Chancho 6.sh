#!/bin/bash
# Baja la última versión de Chancho 6 por Wi-Fi y cambia solo el programa:
# config.xml, mapping.xml, los proyectos y la samplelib no se tocan.
URL="https://github.com/ernestomalaca/chancho6/releases/download/consola/chancho6-rg353v.elf"
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
LOG="$DIR/actualizar.txt"
mkdir -p "$DIR"
cd "$DIR" || exit 1
echo "$(date) actualizando desde $URL" > "$LOG"
TMP="$DIR/chancho6-rg353v.elf.nuevo"
rm -f "$TMP"
if command -v curl >/dev/null 2>&1; then
  curl -fL --retry 3 -o "$TMP" "$URL" >> "$LOG" 2>&1
else
  wget -O "$TMP" "$URL" >> "$LOG" 2>&1
fi
# que sí sea un programa (empieza con ELF), no una página de error
if [ -s "$TMP" ] && [ "$(head -c 4 "$TMP" | tail -c 3)" = "ELF" ]; then
  [ -f chancho6-rg353v.elf ] && mv -f chancho6-rg353v.elf chancho6-rg353v.elf.anterior
  mv -f "$TMP" chancho6-rg353v.elf
  chmod +x chancho6-rg353v.elf
  echo "listo" >> "$LOG"
else
  rm -f "$TMP"
  echo "no se pudo bajar (¿hay Wi-Fi?); se queda la versión que había" >> "$LOG"
fi
sync
