#!/bin/bash
# Abre Chancho 6 anotando cómo va Ableton Link, una vez por tiempo, en
# chancho6/link.txt, y cada 10 s qué programas usan el procesador y a qué
# velocidad va, en chancho6/sistema.txt. Abre Live (o el Move) con Link y su
# metrónomo, abre aquí un proyecto, pon LINK en "si" (Proyecto), dale play y
# déjalo un par de minutos. Luego sal (Proyecto > SALIR).
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
export HOME="$DIR"
export TRK_LINKDBG=1
chmod +x ./chancho6-rg353v.elf 2>/dev/null

{
  echo "=== $(date) ==="
  uname -a
  echo "--- procesador"
  for p in /sys/devices/system/cpu/cpufreq/policy*; do
    echo "$p: $(cat $p/scaling_governor 2>/dev/null) $(cat $p/scaling_min_freq 2>/dev/null)-$(cat $p/scaling_max_freq 2>/dev/null) kHz"
  done
  echo "--- audio"
  cat /proc/asound/cards 2>/dev/null
  pgrep -a pulseaudio; pgrep -a pipewire
} > sistema.txt 2>&1

./chancho6-rg353v.elf > link.txt 2>&1 &
PID=$!
while kill -0 $PID 2>/dev/null; do
  {
    echo "=== $(date +%T) ==="
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
      echo "$(basename $c): $(cat $c/cpufreq/scaling_cur_freq 2>/dev/null) kHz"
    done
    cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null | sed 's/^/temp (m°C): /'
    ps -eo pid,comm,%cpu,%mem,ni,rtprio --sort=-%cpu | head -12
  } >> sistema.txt 2>&1
  sleep 10
done
sync
