#!/bin/bash
# Abre Chancho 6 anotando cada botón que se presiona (DUMPEVENT) en
# chancho6/botones.txt. Presiona en orden: arriba, abajo, izquierda,
# derecha, A, B, X, Y, L, R, Select, Start. Luego sal desde Project > Exit.
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
export HOME="$DIR"
cp -f config.xml config.xml.normal
sed 's|</CONFIG>|\t<DUMPEVENT value="YES"/>\n</CONFIG>|' config.xml.normal > config.xml
./chancho6-rg353v.elf > botones.txt 2>&1
mv -f config.xml.normal config.xml
sync
