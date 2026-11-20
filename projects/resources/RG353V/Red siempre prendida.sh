#!/bin/bash
# Deja prendidos al arrancar los servicios de red de dArkOS (los mismos que
# "Enable Remote Services": Samba para copiar desde la compu, SSH, etc.), para
# no tener que activarlos después de cada reinicio. Lo que hizo queda en
# chancho6/red.txt. Para volver a como estaba: "Red como antes.sh".
OUT="$(cd "$(dirname "$0")" && pwd)/chancho6/red.txt"
S=$(ls /opt/system/*.sh 2>/dev/null | grep -i "remote" | grep -iv "disable" | head -1)
SERV=""
[ -n "$S" ] && SERV=$(grep -oE "systemctl +(start|restart) +[A-Za-z0-9@._-]+" "$S" | awk '{print $NF}' | sort -u)
[ -z "$SERV" ] && SERV="smbd nmbd ssh"
{
  echo "=== $(date) ==="
  echo "script de dArkOS: ${S:-no encontrado}"
  for s in $SERV; do
    sudo systemctl enable "$s" && sudo systemctl start "$s" && echo "prendido al arrancar: $s"
  done
} > "$OUT" 2>&1
echo "$SERV" > "$(dirname "$OUT")/.red_servicios"
sync
