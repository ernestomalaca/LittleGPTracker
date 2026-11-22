#!/bin/bash
# Abre Chancho 6 con el diagnóstico del warp complex: en chancho6/log.txt,
# cuándo se estira cada loop, cuánto tarda, cuándo pasa al tempo nuevo y cada
# bloque de audio que llegó tarde ([LOAD]); en chancho6/warp.txt, cada 2 s,
# temperatura, velocidad del procesador y qué hilos lo usan.
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
export HOME="$DIR"
export TRK_LOADLOG=1
chmod +x ./chancho6-rg353v.elf 2>/dev/null
for f in log warp; do
  [ -f "$f.txt" ] || continue
  mv -f "$f-2.txt" "$f-3.txt" 2>/dev/null
  mv -f "$f-1.txt" "$f-2.txt" 2>/dev/null
  mv -f "$f.txt" "$f-1.txt"
done
echo "=== $(date) (Probar Warp) ===" > log.txt
./chancho6-rg353v.elf >> log.txt 2>&1 &
PID=$!
while kill -0 $PID 2>/dev/null; do
  {
    echo "=== $(date +%T) reloj $(cut -d' ' -f1 /proc/uptime)"
    for t in /sys/class/thermal/thermal_zone*/temp; do echo "temp $(basename $(dirname $t)): $(( $(cat $t 2>/dev/null || echo 0) / 1000 ))C"; done
    for c in /sys/devices/system/cpu/cpu[0-9]*; do echo "$(basename $c): $(( $(cat $c/cpufreq/scaling_cur_freq 2>/dev/null || echo 0) / 1000 )) MHz"; done
    ps -L -o tid,cls,ni,pcpu,comm -p $PID 2>/dev/null | sort -k4 -nr | head -8
  } >> warp.txt 2>&1
  sleep 2
done
wait $PID
CODE=$?
echo "=== $(date +%T) salió con código $CODE" >> log.txt
sync
