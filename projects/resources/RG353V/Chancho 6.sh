#!/bin/bash
# Chancho 6 (tracker) para la Anbernic RG353V. Va en la carpeta de Ports
# junto a la carpeta chancho6/. Los proyectos (lgpt_*) y la samplelib viven
# dentro de chancho6/.
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
export HOME="$DIR"
chmod +x ./chancho6-rg353v.elf 2>/dev/null
# los registros de las 3 veces anteriores se guardan (log-1 es la anterior):
# si algo truena y se vuelve a abrir, lo de esa vez no se pierde
for f in log; do
  [ -f "$f.txt" ] || continue
  mv -f "$f-2.txt" "$f-3.txt" 2>/dev/null
  mv -f "$f-1.txt" "$f-2.txt" 2>/dev/null
  mv -f "$f.txt" "$f-1.txt"
done
echo "=== $(date) ===" > log.txt
./chancho6-rg353v.elf >> log.txt 2>&1
CODE=$?
echo "=== $(date +%T) salió con código $CODE (134 abort, 139 crash de memoria, 137 forzado)" >> log.txt
sync
