#!/bin/bash
# Deshace "Red siempre prendida.sh": los servicios de red vuelven a quedar
# apagados al arrancar (como trae dArkOS); se activan a mano con
# Options > Enable Remote Services.
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
SERV=$(cat "$DIR/.red_servicios" 2>/dev/null)
[ -z "$SERV" ] && SERV="smbd nmbd ssh"
{
  echo "=== $(date) ==="
  for s in $SERV; do sudo systemctl disable "$s" && echo "ya no arranca solo: $s"; done
} > "$DIR/red.txt" 2>&1
sync
