#!/bin/bash
# Chancho 6 (tracker) para la Anbernic RG353V. Va en la carpeta de Ports
# junto a la carpeta chancho6/. Los proyectos (lgpt_*) y la samplelib viven
# dentro de chancho6/.
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
export HOME="$DIR"
chmod +x ./chancho6-rg353v.elf 2>/dev/null
./chancho6-rg353v.elf > log.txt 2>&1
sync
