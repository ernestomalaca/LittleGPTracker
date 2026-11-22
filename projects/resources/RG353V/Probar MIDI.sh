#!/bin/bash
# Prueba si la consola ve un controlador MIDI por USB (no abre Chancho 6).
# Conecta el controlador al USB-C (con adaptador OTG si hace falta), abre
# esto desde Ports y, cuando la pantalla quede negra, toca teclas y mueve
# perillas unos 10 segundos. Vuelve solo al menú. El reporte queda en
# chancho6/midi.txt
DIR="$(cd "$(dirname "$0")" && pwd)/chancho6"
cd "$DIR" || exit 1
OUT="$DIR/midi.txt"
{
  echo "=== $(date) ==="
  uname -a
  echo "--- USB (aparatos conectados)"
  command -v lsusb >/dev/null && lsusb
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/product" ] && echo "$(basename $d): $(cat $d/manufacturer 2>/dev/null) / $(cat $d/product) [$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)]"
  done
  echo "--- modo del USB-C"
  for r in /sys/class/usb_role/*/role; do [ -f "$r" ] && echo "$r: $(cat $r)"; done
  for m in /sys/devices/platform/*usb*/*/dr_mode /proc/device-tree/*usb*/dr_mode; do [ -f "$m" ] && echo "$m: $(tr -d '\0' < $m)"; done
  echo "--- tarjetas de sonido"
  cat /proc/asound/cards 2>/dev/null
  ls -l /dev/snd 2>/dev/null
  echo "--- MIDI (alsa)"
  command -v amidi >/dev/null && amidi -l
  command -v aconnect >/dev/null && aconnect -l
  ls /proc/asound/*/midi* 2>/dev/null && cat /proc/asound/*/midi* 2>/dev/null
  echo "--- módulos y kernel"
  lsmod 2>/dev/null | grep -i -E "snd_usb|midi|seq"
  [ -f /proc/config.gz ] && zcat /proc/config.gz | grep -E "CONFIG_SND_USB_AUDIO|CONFIG_SND_RAWMIDI|CONFIG_SND_SEQUENCER|CONFIG_USB_OTG|CONFIG_USB_DWC|CONFIG_SND_USB_MIDI"
  echo "--- lo último del sistema (usb / midi)"
  dmesg 2>/dev/null | grep -i -E "usb|midi|audio|otg|dwc" | tail -40
  echo "--- lo que llega de cada puerto MIDI (10 s)"
  found=0
  for dev in /dev/snd/midiC*D*; do
    [ -e "$dev" ] || continue
    found=1
    echo "$dev:"
    timeout 10 od -An -tx1 -v -w12 < "$dev" | head -60
  done
  [ $found = 0 ] && echo "ningún /dev/snd/midi*: la consola no ve el controlador como MIDI"
  echo "=== fin ==="
} > "$OUT" 2>&1
sync
