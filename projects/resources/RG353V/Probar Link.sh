#!/bin/bash
# Abre Chancho 6 anotando cómo va Ableton Link, una vez por tiempo, en
# chancho6/link.txt. Abre Live con Link prendido y su metrónomo, abre aquí
# un proyecto, pon LINK en "si" (Proyecto), dale play y déjalo ~30 segundos.
# Luego sal (Proyecto > SALIR) y baja link.txt desde chancho.local:8080.
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
export HOME="$DIR"
export TRK_LINKDBG=1
chmod +x ./chancho6-rg353v.elf 2>/dev/null
./chancho6-rg353v.elf > link.txt 2>&1
sync
