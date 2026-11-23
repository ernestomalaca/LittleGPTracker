#!/usr/bin/env python3
# live2chancho: un set de Live (vista de sesión, pistas de audio) -> proyecto de Chancho 6
#
# uso: python3 live2chancho.py "mi set.als" carpeta_destino [plantilla.dat]
#
# Lo que pasa:
#  - cada pista de audio (máx. 8) -> pista de audio de Chancho, con su nombre
#  - cada clip de audio -> su propio clip en su celda (misma pista y escena),
#    independiente como en Live; si el audio es idéntico a otro ya convertido,
#    el WAV se escribe una sola vez y los dos clips lo usan
#    * se recorta la región del loop (o del clip si no tiene loop) a un WAV
#      de 44.1 kHz / 16 bits en samples/, así Chancho lo toca exacto
#    * compases = largo en beats / 4; el audio se estira (conservando el
#      tono) al tempo entero del proyecto, así Chancho lo toca sin estirar;
#      Re-Pitch -> re-pitch, lo demás -> complex
#    * el volumen de la pista de Live -> la ganancia del clip ("a gain", dB)
#  - el tempo del set -> el del proyecto (redondeado a entero)
# Lo que no pasa: efectos, envíos, automatización, clips MIDI, lo que suena
# antes del loop (Chancho toca solo el loop), warps con varios markers
# distintos (se toma el tempo promedio de la región).

import sys, os, gzip, math, re, subprocess, xml.etree.ElementTree as ET

AUDIO_CLIP_BASE = 0x40
MAX_CLIPS = 0xFF - 0x40   # A00..ABE (191 clips de audio)
SR = 44100


def val(e, path, default=None):
    x = e.find(path)
    return x.get('Value') if x is not None and x.get('Value') is not None else default


def beats_to_secs(markers, beat):
    """los warp markers (seg, beat) -> en qué segundo del audio cae ese beat"""
    if len(markers) < 2:
        return None
    for (s0, b0), (s1, b1) in zip(markers, markers[1:]):
        if beat <= b1 or (s1, b1) == markers[-1]:
            if b1 == b0:
                return s0
            return s0 + (beat - b0) * (s1 - s0) / (b1 - b0)
    return None


def plain(s):
    import unicodedata
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode()
    return s.replace('"', '').replace('<', '').replace('>', '').replace('&', '')


def exact(path, frames):
    import wave
    w = wave.open(path)
    p = w.getparams()
    data = w.readframes(w.getnframes())
    w.close()
    size = frames * p.nchannels * p.sampwidth
    data = data[:size] + b'\0' * max(0, size - len(data))
    o = wave.open(path, 'wb')
    o.setparams(p)
    o.writeframes(data)
    o.close()


def safe(name, n=24):
    name = re.sub(r'[^A-Za-z0-9_-]+', '_', plain(name)).strip('_')
    return (name or 'clip')[:n]


def main():
    als, dest = sys.argv[1], sys.argv[2]
    here = os.path.dirname(os.path.abspath(__file__))
    tpl = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, 'plantilla_chancho.dat')
    base = os.path.dirname(os.path.abspath(als))
    root = ET.fromstring(gzip.open(als).read())
    ls = root.find('LiveSet')
    tempo = float(val(ls, './/MainTrack//Tempo/Manual') or val(ls, './/MasterTrack//Tempo/Manual') or 120)
    target = int(round(tempo))   # Chancho va en bpm enteros: los audios se ajustan a este
    tracks = [t for t in ls.find('Tracks') if t.tag == 'AudioTrack'][:8]
    os.makedirs(os.path.join(dest, 'samples'), exist_ok=True)

    song = bytearray([0xFF] * (8 * 256))
    instruments = []          # (id, params)
    names = []
    report = []
    nclip = 0
    seen = {}                 # audio idéntico -> el WAV que ya está (se escribe una vez)
    import hashlib, tempfile, shutil
    tmpdir = tempfile.mkdtemp(prefix='live2chancho')
    for ti, t in enumerate(tracks):
        tname = val(t, 'Name/EffectiveName', 'AUDIO %d' % (ti + 1))
        names.append(tname)
        vol = float(val(t, './/Mixer/Volume/Manual', 1) or 1)
        gain = int(round(20 * math.log10(vol))) if vol > 0 else -60
        slots = t.findall('.//MainSequencer/ClipSlotList/ClipSlot')
        for row, s in enumerate(slots):
            c = s.find('ClipSlot/Value/AudioClip')
            if c is None or row >= 256:
                continue
            cname = val(c, 'Name', '')
            fr = c.find('SampleRef/FileRef')
            rel = val(fr, 'RelativePath')
            path = os.path.join(base, rel) if rel else val(fr, 'Path')
            if not path or not os.path.exists(path):
                report.append('  %s / %s: no encuentro el audio (%s)' % (tname, cname, rel or path))
                continue
            warped = val(c, 'IsWarped') == 'true'
            mode = int(val(c, 'WarpMode', 0) or 0)
            markers = sorted((float(w.get('SecTime')), float(w.get('BeatTime')))
                             for w in c.findall('WarpMarkers/WarpMarker'))
            if val(c, 'Loop/LoopOn') == 'true':
                b0, b1 = float(val(c, 'Loop/LoopStart')), float(val(c, 'Loop/LoopEnd'))
            else:
                b0, b1 = float(val(c, 'CurrentStart')), float(val(c, 'CurrentEnd'))
            if warped and len(markers) >= 2:
                s0, s1 = beats_to_secs(markers, b0), beats_to_secs(markers, b1)
            else:   # sin warp: los "beats" del clip son segundos al tempo del set
                s0, s1 = b0 * 60.0 / tempo, b1 * 60.0 / tempo
            beats = b1 - b0
            bars = max(1, min(16, int(round(beats / 4.0))))
            secs = s1 - s0
            srcbpm = beats * 60.0 / secs if secs > 0 else tempo
            bpm = target
            fname = '%s_%02d.wav' % (safe(tname, 12), row)
            out = os.path.join(tmpdir, fname)
            # el loop ya estirado (conservando el tono) a 'target' bpm exactos:
            # en Chancho suena tal cual, sin estirar en la consola
            frames = int(round(beats * 60.0 / target * SR))
            ratio = target / srcbpm      # >1: más rápido (más corto)
            af = 'aresample=%d' % SR
            if abs(ratio - 1.0) > 1e-6:
                af = 'rubberband=tempo=%.8f:pitchq=quality:window=standard,' % ratio + af
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', '%.6f' % s0, '-t', '%.6f' % secs,
                            '-i', path, '-af', af, '-c:a', 'pcm_s16le', out], check=True)
            exact(out, frames)   # el largo exacto (el estirado deja unos cuadros de más o de menos)
            import wave
            w = wave.open(out)
            key = hashlib.md5(w.readframes(w.getnframes())).hexdigest()
            w.close()
            warp = 're-pitch' if mode == 3 else 'complex'
            if nclip >= MAX_CLIPS:
                report.append('  %-18s escena %02X  sin lugar (máx. %d clips de audio)' % (tname[:18], row, MAX_CLIPS))
                os.remove(out)
                continue
            same = ''
            if key in seen:   # el mismo audio ya está escrito: este clip lo usa (cada clip es independiente)
                os.remove(out)
                fname = seen[key]
                same = '  (audio = %s)' % fname
            else:
                seen[key] = fname
                shutil.move(out, os.path.join(dest, 'samples', fname))
            iid = AUDIO_CLIP_BASE + nclip
            song[ti + 8 * row] = iid
            instruments.append((iid, {
                'sample': fname, 'start': 0, 'loopstart': 0, 'end': frames,
                'loopmode': 'none', 'loop bars': bars, 'loop bpm': bpm, 'from bar': 1,
                'warp': warp, 'length': bars,
                'a gain': gain, 'audio clip': 1, 'a init': 1,
            }))
            report.append('  %-18s escena %02X  %-26s A%02X  %2d compases  %.2f -> %d bpm  %s  %+d dB' %
                          (tname[:18], row, cname[:26], iid - AUDIO_CLIP_BASE, bars, srcbpm, bpm, warp, gain) + same)
            nclip += 1

    # --- el proyecto, sobre la plantilla
    txt = open(tpl, encoding='utf-8').read()
    def setp(name, value):
        nonlocal txt
        txt = re.sub(r'(<PARAMETER NAME="%s" VALUE=")[^"]*(")' % re.escape(name),
                     lambda m: m.group(1) + str(value) + m.group(2), txt, count=1)
    setp('tempo', target)
    for i in range(8):
        on = i < len(tracks)
        setp('track%d' % (i + 1), 'AUD' if on else 'OFF')
        setp('trackname%d' % (i + 1), (plain(names[i])[:12] if on else ''))
        setp('trackinstr%d' % (i + 1), -1)
        setp('trackcolor%d' % (i + 1), i if on else -1)
    # la Session
    chunks = ''.join('            <DATA>%s</DATA>\n' % song[k:k + 64].hex().upper() for k in range(0, len(song), 64))
    txt = re.sub(r'(<SONG>\s*<SONG>\n).*?(\s*</SONG>)', lambda m: m.group(1) + chunks.rstrip('\n') + m.group(2),
                 txt, count=1, flags=re.S)
    txt = re.sub(r'<CLIPS>.*?</CLIPS>', '<CLIPS>\n        </CLIPS>', txt, count=1, flags=re.S)
    # los instrumentos: fuera los Sample de la plantilla, entran los loops
    model = None
    for m in re.finditer(r'(        <INSTRUMENT ID="[0-9A-F]+" TYPE="Sample">.*?</INSTRUMENT>\n)', txt, flags=re.S):
        if '<PARAM NAME="audio clip" VALUE="1"' in m.group(1):
            model = m.group(1)
            break
    if model is None:
        sys.exit('la plantilla no trae ningún loop de audio para copiar')
    txt = re.sub(r'        <INSTRUMENT ID="[0-9A-F]+" TYPE="Sample">.*?</INSTRUMENT>\n', '', txt, flags=re.S)
    # los MIDI out de la plantilla (en proyectos viejos iban en 80-8F, donde
    # ahora caen clips de audio): fuera, Chancho los crea solo
    txt = re.sub(r'        <INSTRUMENT ID="[0-9A-F]+" TYPE="Midi">.*?</INSTRUMENT>\n', '', txt, flags=re.S)
    blocks = ''
    for iid, prm in instruments:
        b = re.sub(r'ID="[0-9A-F]+"', 'ID="%02X"' % iid, model, count=1)
        for k, v in prm.items():
            b = re.sub(r'(<PARAM NAME="%s" VALUE=")[^"]*(")' % re.escape(k),
                       lambda mm: mm.group(1) + str(v) + mm.group(2), b, count=1)
        blocks += b
    txt = txt.replace('    <INSTRUMENTBANK>\n', '    <INSTRUMENTBANK>\n' + blocks, 1)
    open(os.path.join(dest, 'lgptsav.dat'), 'w', encoding='utf-8').write(txt)
    ref = os.path.join(dest, 'samples.ref')
    if os.path.exists(ref):
        os.remove(ref)
    used = sum(1 for b in song if b != 0xFF)
    print('tempo %.2f -> %d, %d pistas, %d clips de audio (de %d), %d audios distintos' %
          (tempo, target, len(tracks), nclip, MAX_CLIPS, len(seen)))
    shutil.rmtree(tmpdir, ignore_errors=True)
    print('\n'.join(report))


if __name__ == '__main__':
    main()
