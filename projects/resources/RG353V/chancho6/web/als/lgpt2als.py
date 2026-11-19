"""lgpt2als — convierte un proyecto de Little Piggy Tracker (lgptsav.dat) en un Live Set.

Uso:  python3 lgpt2als.py <carpeta lgpt_*> plantilla.als salida.als [--real-dir=/ruta/real/del/proyecto]

Reglas:
  - Cada canal de LGPT es una pista de Live (8 canales = 8 pistas).
  - Cada fila de la Song es una escena; cada chain es un clip (phrases aplanadas con su transposición).
  - Canal con un solo instrumento  -> Simpler, con las notas tal cual (melódico).
  - Canal con varios instrumentos  -> Drum Rack: un pad con Drum Sampler por instrumento (máx. 16).
  - Canal con instrumentos "looper sync" -> pista de audio, clip con warp Re-Pitch.
  - Pistas de audio del fork (Audio tracks en Project): cada celda es un loop -> clip de audio.
  - Comandos: PAN -> paneo, FCUT/FRES -> Auto Filter, VOLM/MVEL -> velocity, KILL -> corta la nota.
"""
import copy, math, os, re, sys
import xml.etree.ElementTree as ET
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trk2als as T

CHANNELS, ROWS, STEPS = 8, 256, 16
EMPTY = 0xFF
# Niveles medidos en el tracker con un sample a escala completa: el Sampler a
# 0 dB sale a -10.3 dB y un loop de pista de audio (volumen 128) a -9.3 dB.
# En Live los faders quedan en 0 dB y la diferencia va en el Simpler / el clip
SAMPLER_DB = -10.3
AUDIO_DB = -9.3

SIMPLER_PROTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "simpler_proto.xml")
# Sampler de Live (varios samples por zonas), sacado de un set del usuario
SAMPLER_PROTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sampler_proto.xml")


# ─────────────────────────── lectura de lgptsav.dat ──────────────────────────
# las escalas de Live en su orden (como Model/Scale.cpp de Chancho 6); la 0 es
# "sin escala" y el resto va a ScaleInformation/Name como índice - 1
LIVE_SCALES = ["Off", "Major", "Minor", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Locrian",
               "Whole Tone", "Half-whole Dim.", "Whole-half Dim.", "Minor Blues", "Minor Pentatonic",
               "Major Pentatonic", "Harmonic Minor", "Harmonic Major", "Dorian #4", "Phrygian Dominant",
               "Melodic Minor", "Lydian Augmented", "Lydian Dominant", "Super Locrian", "8-Tone Spanish",
               "Bhairav", "Hungarian Minor", "Hirajoshi", "In-Sen", "Iwato", "Kumoi", "Pelog Selisir",
               "Pelog Tembung", "Messiaen 3", "Messiaen 4", "Messiaen 5", "Messiaen 6", "Messiaen 7"]
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def apply_scale(L, scale, root):
    """La escala del proyecto en el set y en todos los clips (Scale Awareness de Live)."""
    on = "true" if scale > 0 else "false"
    for si in L.root.iter("ScaleInformation"):
        si.find("Root").set("Value", str(root if scale > 0 else 0))
        si.find("Name").set("Value", str(scale - 1 if scale > 0 else 0))
    for tag in ("IsInKey", "InKey"):
        for e in L.root.iter(tag):
            e.set("Value", on)


def hexbuf(node):
    out = bytearray()
    for d in node.findall("DATA"):
        if d.get("VALUE") is not None:
            out += bytes([int(d.get("VALUE")) & 0xFF]) * int(d.get("LENGTH"))
        else:
            out += bytes.fromhex((d.text or "").strip())
    return bytes(out)


class LGPT:
    def __init__(self, folder):
        self.folder = folder
        root = T.ET.parse(os.path.join(folder, "lgptsav.dat")).getroot()
        self.tempo = 120.0
        audio_tracks = 0                      # proyectos de antes de los tipos de pista
        self.types = ["TRK"] * CHANNELS       # TRK tracker, AUD audio (loops), MID notas, OFF no existe
        self.owner = [None] * CHANNELS        # instrumento de la pista (None: manda la phrase)
        self.names = [""] * CHANNELS          # nombre de la pista (el del preset)
        self.scale, self.scale_root = 0, 0    # escala del proyecto (0 = sin escala)
        for p in root.iter("PARAMETER"):
            name = p.get("NAME")
            if name == "tempo":
                self.tempo = float(p.get("VALUE"))
            elif name == "scale":
                v = (p.get("VALUE") or "").lower()
                self.scale = next((i for i, n in enumerate(LIVE_SCALES) if n.lower() == v), 0)
            elif name == "scaleroot":
                v = (p.get("VALUE") or "").upper()
                self.scale_root = NOTE_NAMES.index(v) if v in NOTE_NAMES else 0
            elif name == "audiotracks":
                audio_tracks = max(0, min(CHANNELS, int(p.get("VALUE"))))
            elif name.startswith("track") and name[5:].isdigit() and 1 <= int(name[5:]) <= CHANNELS:
                self.types[int(name[5:]) - 1] = p.get("VALUE")
            elif name.startswith("trackinstr") and name[10:].isdigit() and 1 <= int(name[10:]) <= CHANNELS:
                v = int(p.get("VALUE") or -1)          # la pista es dueña de su instrumento
                self.owner[int(name[10:]) - 1] = v if v >= 0 else None
            elif name.startswith("trackname") and name[9:].isdigit() and 1 <= int(name[9:]) <= CHANNELS:
                self.names[int(name[9:]) - 1] = p.get("VALUE") or ""
        for ch in range(CHANNELS - audio_tracks, CHANNELS):
            self.types[ch] = "AUD"
        song = root.find("SONG")
        self.transpose = 0
        for p in root.iter("PARAMETER"):
            if p.get("NAME") == "transpose":
                try:
                    self.transpose = int(p.get("VALUE"))
                except (TypeError, ValueError):
                    pass
        # Chancho 6: clips MIDI propios (<CLIPS><CLIP ID PAGES NAME LOOPSTART LOOPEND NOTES>)
        clips_el = song.find("CLIPS")
        self.clip_model = clips_el is not None
        if self.clip_model:
            self.song = hexbuf(song.find("SONG"))
            self.clips, self.chain_names, self.chain_loops = {}, {}, {}
            for c in clips_el.findall("CLIP"):
                cid = int(c.get("ID"), 16)
                raw = bytes.fromhex(c.get("NOTES") or "")
                notes = []
                for k in range(0, len(raw) - 6, 7):
                    st, tick, ln, pitch, vel, rat, ramp = raw[k:k + 7]
                    notes.append(dict(start=st, tick=tick, len=ln + 1, pitch=pitch, vel=vel,
                                      rat=rat + 1, ramp=ramp - 256 if ramp > 127 else ramp))
                self.clips[cid] = dict(pages=int(c.get("PAGES") or 1), notes=notes)
                if c.get("NAME"):
                    self.chain_names[cid] = c.get("NAME")
                if c.get("LOOPSTART") is not None:
                    a, z = int(c.get("LOOPSTART")), int(c.get("LOOPEND"))
                    if 0 <= a <= z:
                        self.chain_loops[cid] = (a, z)
            self.instruments = {}
            for ins in root.iter("INSTRUMENT"):
                params = {p.get("NAME"): p.get("VALUE") for p in ins.findall("PARAM")}
                self.instruments[int(ins.get("ID"), 16)] = dict(type=ins.get("TYPE"), **params)
            return
        b = {c.tag: hexbuf(c) for c in song if c.tag not in ("CHAINNAMES", "CHAINLOOPS")}
        self.chain_loops = {}                 # loop del clip: (página inicio, página fin)
        for l in song.iter("LOOP"):
            try:
                c, a, z = int(l.get("ID"), 16), int(l.get("START")), int(l.get("END"))
                if 0 <= a <= z < 16:
                    self.chain_loops[c] = (a, z)
            except (TypeError, ValueError):
                pass
        self.chain_names = {}                 # nombres de los chains ("woodblock 1")
        for n in song.iter("NAME"):
            try:
                self.chain_names[int(n.get("ID"), 16)] = n.get("VALUE") or ""
            except (TypeError, ValueError):
                pass
        self.song = b["SONG"]
        self.chains, self.transposes = b["CHAINS"], b["TRANSPOSES"]
        self.notes, self.instr = b["NOTES"], b["INSTRUMENTS"]
        self.cmd = [b["COMMAND1"], b["COMMAND2"]]
        # capas de voces de Chancho 6 (acordes): voz 1..3, 255 phrases x 16 pasos
        self.vnotes, self.vvels, self.vflags = b.get("VNOTES"), b.get("VVELS"), b.get("VFLAGS")
        # nudge (sextos de paso) y subdivisiones de cada nota: voz 0..3
        self.timing = b.get("NTIMING")
        self.ramp = b.get("NRAMP")              # rampa de velocity de las subdivisiones
        self.fine = b.get("NFINE")              # nudge fino: cuartos de sexto (1/24 de paso)
        self.par = [b["PARAM1"], b["PARAM2"]]
        self.instruments = {}
        for ins in root.iter("INSTRUMENT"):
            params = {p.get("NAME"): p.get("VALUE") for p in ins.findall("PARAM")}
            self.instruments[int(ins.get("ID"), 16)] = dict(type=ins.get("TYPE"), **params)

    def is_audio(self, ch):
        """Canal de audio del fork trk2als: sus celdas son instrumentos (loops), no chains."""
        return self.types[ch] == "AUD"

    def loop_bars(self, ins):
        try:
            return max(1, min(16, int(self.instruments.get(ins, {}).get("loop bars", 1))))
        except ValueError:
            return 1

    def clip_bars(self, ins):
        """Cuántos compases dura la celda en la song (campo 'length'); 0 = los del loop."""
        try:
            n = int(self.instruments.get(ins, {}).get("length", 0))
        except ValueError:
            n = 0
        return max(1, min(16, n)) if n > 0 else self.loop_bars(ins)

    def cell(self, row, ch):
        return self.song[row * CHANNELS + ch]

    def chain_phrases(self, c):
        out = []
        for i in range(16):
            p = self.chains[c * 16 + i]
            if p == EMPTY:
                break
            out.append((p, (self.transposes[c * 16 + i] + 128) % 256 - 128))
        return out

    def step(self, phrase, s):
        i = phrase * 16 + s
        cmds = []
        for k in (0, 1):
            fourcc = self.cmd[k][i * 4:i * 4 + 4].decode("latin-1")
            # LGPT guarda los parámetros tal como están en memoria (little-endian)
            param = self.par[k][i * 2] | (self.par[k][i * 2 + 1] << 8)
            if fourcc.strip("-"):
                cmds.append((fourcc, param))
        return self.notes[i], self.instr[i], cmds

    def last_row(self):
        last = -1
        for r in range(ROWS):
            if any(self.cell(r, ch) != EMPTY for ch in range(CHANNELS)):
                last = r
        return last


# ─────────────────────────────── conversión ──────────────────────────────────
def flatten_chain(lg, chain, last_instr, stats, owner=None):
    """Devuelve eventos [(t, nota, instrumento, vel)], cortes [t], comandos [(t, letra, valor)] y largo.
    Con 'owner' (la pista es dueña de su instrumento) la columna de instrumento no cuenta."""
    events, kills, cmds = [], [], []
    for i, (ph, tr) in enumerate(lg.chain_phrases(chain)):
        for s in range(STEPS):
            t = i * T.PHRASE_LEN + s * T.STEP
            note, ins, cs = lg.step(ph, s)
            vel = 100
            for fourcc, param in cs:
                v = param & 0xFF
                if fourcc == "PAN ":
                    cmds.append((t, "P", min(255, round(v * 255 / 254))))
                elif fourcc == "FCUT":
                    cmds.append((t, "F", v))
                elif fourcc == "FRES":
                    cmds.append((t, "Q", v))
                elif fourcc == "VOLM":
                    vel = max(1, min(127, round(v * 127 / 255)))
                elif fourcc == "MVEL":   # velocity del piano roll de Chancho 6
                    vel = max(1, min(127, v // 2))
                elif fourcc == "KILL":
                    kills.append(t)
                else:
                    stats[fourcc] += 1
            if owner is not None:
                last_instr[0] = owner
            elif ins != EMPTY:
                last_instr[0] = ins
            if note != EMPTY and last_instr[0] is not None:
                events.append((t, note + tr, last_instr[0], vel))
    length = len(lg.chain_phrases(chain)) * T.PHRASE_LEN
    return events, kills, sorted(cmds), length


def voice_notes(lg, chain, instr):
    """Notas de las capas de acordes (voces 1..3): cada una suena hasta la
    siguiente nota de su voz, su corte o el final del clip."""
    if not lg.vnotes or instr is None:
        return []
    out = []
    phrases = lg.chain_phrases(chain)
    length = len(phrases) * T.PHRASE_LEN
    for v in range(1, 4):
        cur = None                                   # (t, nota, vel) sonando
        for i, (ph, tr) in enumerate(phrases):
            for s in range(STEPS):
                t = i * T.PHRASE_LEN + s * T.STEP
                k = ((v - 1) * 255 + ph) * 16 + s
                note, vel, flag = lg.vnotes[k], lg.vvels[k], lg.vflags[k]
                if cur and (note != EMPTY or flag & 1):
                    out.append((cur[0], t - cur[0], cur[1], instr, cur[2]))
                    cur = None
                if note != EMPTY:
                    cur = (t, note + tr, 100 if vel == 0xFF else max(1, min(127, vel)))
        if cur:
            out.append((cur[0], length - cur[0], cur[1], instr, cur[2]))
    return out


def note_timing(lg, chain):
    """{(t, nota): (sextos de nudge, con fracción; subdivisiones; rampa)} de las notas del clip que los tienen."""
    out = {}
    if not lg.timing and not lg.fine:
        return out
    for i, (ph, tr) in enumerate(lg.chain_phrases(chain)):
        for s in range(STEPS):
            t = i * T.PHRASE_LEN + s * T.STEP
            for v in range(4):
                b = lg.timing[(v * 255 + ph) * 16 + s] if lg.timing else 0
                fine = (lg.fine[(v * 255 + ph) * 16 + s] & 3) if lg.fine else 0
                if not b and not fine:
                    continue
                if v == 0:
                    note = lg.notes[ph * 16 + s]
                else:
                    note = lg.vnotes[((v - 1) * 255 + ph) * 16 + s] if lg.vnotes else EMPTY
                if note != EMPTY:
                    r = lg.ramp[(v * 255 + ph) * 16 + s] if lg.ramp else 0
                    r = r - 256 if r > 127 else r
                    out[(t, note + tr)] = (min(5, b & 0x0F) + fine / 4, ((b >> 4) & 7) + 1, r)
    return out


def apply_timing(notes, timing):
    """Nudge: la nota entera se recorre. Subdivisión: la nota se reparte en n
    golpes iguales, con su rampa de velocity, como suena en la consola."""
    if not timing:
        return notes
    out = []
    for t, dur, note, ins, vel in notes:
        tick, n, ramp = timing.get((t, note), (0, 1, 0))
        start = t + tick * T.STEP / 6
        for k in range(n):
            v = vel
            if ramp and n > 1:
                amt = 0.2 * abs(ramp)
                f = 1 - amt * (n - 1 - k) / (n - 1) if ramp > 0 else 1 - amt * k / (n - 1)
                v = max(1, int(vel * f + 0.5))
            out.append((start + dur * k / n, dur / n, note, ins, v))
    return out


def clip_notes(lg, cid, ins):
    """Chancho 6: las notas de un clip [(t, dur, nota, instrumento, vel)] con el
    nudge (1/24 de paso) y las subdivisiones con su rampa, como suenan."""
    out = []
    for n in lg.clips[cid]["notes"]:
        note = n["pitch"] + lg.transpose
        if not 0 <= note <= 127:
            continue
        start = n["start"] * T.STEP + n["tick"] * T.STEP / 24
        dur = n["len"] * T.STEP
        r, ramp = n["rat"], n["ramp"]
        for h in range(r):
            v = n["vel"]
            if r > 1 and ramp:
                amt = 0.2 * abs(ramp)
                f = 1 - amt * (r - 1 - h) / (r - 1) if ramp > 0 else 1 - amt * h / (r - 1)
                v = max(1, int(v * f + 0.5))
            out.append((start + dur * h / r, dur / r, note, ins, max(1, min(127, v))))
    return out


def durations(events, kills, length):
    """Canal monofónico: cada nota suena hasta la siguiente nota, un KILL o el final del clip."""
    cuts = sorted(set([e[0] for e in events] + kills + [length]))
    out = []
    for t, note, ins, vel in events:
        end = next(c for c in cuts if c > t)
        out.append((t, end - t, note, ins, vel))
    return out


def sample_beats(lg, ins, note, exact=False):
    """Lo que dura el sample tocado en esa nota, en beats y redondeado a pasos.
    Así un one-shot no deja una barra larguísima en el piano roll. Los samples en loop no se recortan."""
    info = lg.instruments.get(ins, {})
    if info.get("type") != "Sample" or info.get("loopmode", "none") not in ("0", "none", "None"):
        return float("inf")
    try:
        s = L_samples.use(info["sample"])
    except (Exception, SystemExit):
        return float("inf")
    root = int(info.get("root note", 60))
    if info.get("s init") not in (None, "0"):
        # pantalla Sampler: suena la región start..end (y el release en Classic)
        def num(k, d=0):
            try:
                return float(info.get(k, d))
            except (TypeError, ValueError):
                return d
        mode = info.get("s mode", "classic")
        if mode == "classic" and info.get("s loop") == "on":
            return float("inf")
        region = max(0.0, num("s end", 100) - num("s start")) / 100.0
        secs = s["frames"] * region / s["sr"]
        warp = info.get("s warp", "off")
        if warp != "off":
            bpm = num("s bpm")
            if bpm <= 0:
                return float("inf")
            secs = secs * bpm / lg.tempo                  # la región dura sus beats a cualquier tempo
            if warp == "re-pitch":
                pass                                       # re-pitch no sigue las notas
        else:
            semis = note - root + num("s transpose") + num("s detune") / 100.0
            secs /= 2 ** (semis / 12)
        if mode == "classic":
            secs += num("s release") / 1000.0
    else:
        secs = s["frames"] / s["sr"] / 2 ** ((note - root) / 12)
    if exact:                                  # lo que dura de verdad, en beats
        return secs * lg.tempo / 60
    steps = max(1, math.ceil(secs * lg.tempo / 60 / T.STEP - 1e-6))
    return steps * T.STEP


def loop_clip(lg, proto_audio, cid, chain, notes, length, ch):
    """Instrumento "looper sync" de LGPT -> clip de audio con warp Re-Pitch.
    LGPT estira la región loopstart..end a 1 compás cambiando la velocidad; la nota la acelera
    o la frena (12 semitonos abajo = 2 compases). En Live la región dura esos beats y el clip
    hace loop sobre ella, igual que en el Piggy."""
    t0, _, note, ins, _ = notes[0]
    info = lg.instruments[ins]
    region = 4 * lg.loop_bars(ins) / 2 ** ((note - int(info.get("root note", 60))) / 12)
    clip = audio_clip(info, proto_audio, cid, region, lg.chain_names.get(chain) or f"{chain:02X}")
    # un clip de audio solo puede ser un loop continuo desde el inicio
    odd = [t for t, *_ in notes if (t - t0) % region > 1e-6]
    other = any(lg.instruments[n[3]].get("sample") != info.get("sample") for n in notes)
    if t0 or odd or other or len({n[2] for n in notes}) > 1:
        print(f"  aviso: canal {ch} chain {chain:02X}: el loop no arranca en el paso 0, se redispara "
              f"fuera de tiempo o cambia de nota/sample; en Live queda como loop continuo")
    return clip


def audio_clip(info, proto_audio, cid, region, label):
    """Clip de audio: la región loopstart..end del sample dura `region` beats y hace loop.
    Warp como en el tracker: Re-Pitch, o Complex con su transposición."""
    s = L_samples.use(info["sample"])
    a = int(info.get("loopstart", 0)) / s["sr"]
    b = (int(info.get("end", 0)) or s["frames"]) / s["sr"]
    clip = copy.deepcopy(proto_audio)
    clip.set("Id", str(cid))
    # con nombre propio (chain "woodblock 1" o el archivo) va solo; si es un número, número + archivo
    T.set_val(clip, "Name", (label if not re.fullmatch(r"A?[0-9A-F]{2}", label) else f"{label} {s['name']}")[:40])
    L_samples.fill_ref(clip.find("SampleRef"), s)
    for path in ("CurrentStart", "Loop/LoopStart", "Loop/StartRelative", "Loop/HiddenLoopStart"):
        T.set_val(clip, path, 0)
    for path in ("CurrentEnd", "Loop/LoopEnd", "Loop/OutMarker", "Loop/HiddenLoopEnd"):
        T.set_val(clip, path, f"{region:g}")
    T.set_val(clip, "Loop/LoopOn", "true")
    T.set_val(clip, "IsWarped", "true")
    if info.get("a init") not in (None, "0"):         # pantalla de clip: gain en dB
        try:
            db = float(info.get("a gain", 0))
        except ValueError:
            db = 0.0
        gain = 10 ** ((AUDIO_DB + db) / 20)
    else:                                            # 128 = el nivel normal del tracker
        vol = int(info.get("volume", 128) or 128)
        gain = 10 ** (AUDIO_DB / 20) * vol / 128.0
    T.set_val(clip, "SampleVolume", f"{max(gain, 0.0003):.6g}")
    T.set_val(clip, "Fade", "false" if info.get("a fade") == "off" else "true")
    if info.get("warp") == "complex":
        T.set_val(clip, "WarpMode", 4)                                   # Complex
        T.set_val(clip, "PitchCoarse", int(info.get("transpose", 0) or 0))
        T.set_val(clip, "PitchFine", int(info.get("a detune", 0) or 0))
    else:
        T.set_val(clip, "WarpMode", 3)                                   # Re-Pitch: la altura la da el tempo
        T.set_val(clip, "PitchCoarse", 0)
        T.set_val(clip, "PitchFine", 0)
    wm = clip.find("WarpMarkers")
    for w in list(wm):
        wm.remove(w)
    spb = (b - a) / region
    for i, (sec, beat) in enumerate([(a, 0), (b, region), (b + spb / 32, region + 1 / 32)]):
        ET.SubElement(wm, "WarpMarker", Id=str(i), SecTime=repr(sec), BeatTime=f"{beat:g}")
    return clip


def sampler_db(ins):
    """Volumen del instrumento en dB respecto al nivel normal del tracker."""
    if ins.get("s init") not in (None, "0"):
        try:
            return float(ins.get("s volume", 0))
        except ValueError:
            return 0.0
    vol = int(ins.get("volume", 128) or 128)
    return 20 * math.log10(vol / 128.0) if vol > 0 else -36.0


def load_simpler():
    return T.ET.parse(SIMPLER_PROTO).getroot()


def load_sampler():
    return T.ET.parse(SAMPLER_PROTO).getroot()


def parse_pads(ins):
    """'s pads' del editor de kits: {nota: dict(gain dB, tune st, pan -50..50, decay ms, choke, start %)}."""
    out = {}
    for item in (ins.get("s pads") or "").split(";"):
        f = item.split(":")
        if len(f) == 7:
            try:
                n, g, t, p, d, c, st = map(int, f)
            except ValueError:
                continue
            out[n] = dict(gain=g, tune=t, pan=p, decay=d, choke=c, start=st)
    return out


def parse_zones(ins):
    """'s zones' del tracker: [(lo, hi, raíz, cents, loopIni, loopFin, wav)]."""
    out = []
    for item in (ins.get("s zones") or "").split(";"):
        f = item.split(",", 6)
        if len(f) < 7:
            continue
        try:
            out.append((int(f[0]), int(f[1]), int(f[2]), float(f[3]), int(f[4]), int(f[5]), f[6]))
        except ValueError:
            pass
    return out


def configure_sampler(sampler, zones, ins, fill_ref, use_sample, name):
    """Instrumento soundfont del tracker -> Sampler de Live: lo mismo que el
    Simpler (envolventes, filtro, glide, voces, volumen) y una parte de sample
    por zona, con su rango de teclas, raíz, afinación y loop."""
    first = use_sample(zones[0][6])
    ins2 = dict(ins)
    ins2["s mode"], ins2["s loop"], ins2["s start"], ins2["s end"] = "classic", "off", "0", "100"
    configure_simpler(sampler, first, 60, ins2, fill_ref)
    T.set_val(sampler, "UserName", name)
    parts = sampler.find("Player/MultiSampleMap/SampleParts")
    proto = parts.find("MultiSamplePart")
    parts.remove(proto)
    for i, (lo, hi, root, cents, ls, le, wav) in enumerate(zones):
        sample = use_sample(wav)
        part = copy.deepcopy(proto)
        part.set("Id", str(i))
        T.set_val(part, "Name", os.path.splitext(wav)[0])
        for tag, val in (("Min", lo), ("Max", hi), ("CrossfadeMin", lo), ("CrossfadeMax", hi)):
            T.set_val(part, "KeyRange/" + tag, val)
        # el tracker toca nota - raíz + cents/100; Live, nota - RootKey + Detune/100
        k = int(round(cents / 100.0))
        T.set_val(part, "RootKey", root - k)
        T.set_val(part, "Detune", int(round(cents - 100 * k)))
        frames = sample["frames"]
        T.set_val(part, "SampleStart", 0)
        T.set_val(part, "SampleEnd", max(1, frames - 1))
        loop = 0 <= ls < le <= frames
        T.set_val(part, "SustainLoop/Start", ls if loop else 0)
        T.set_val(part, "SustainLoop/End", le if loop else max(1, frames - 1))
        T.set_val(part, "SustainLoop/Mode", 1 if loop else 0)    # 1 = loop hacia adelante
        T.set_val(part, "ReleaseLoop/Start", 0)
        T.set_val(part, "ReleaseLoop/End", max(1, frames - 1))
        dur = frames / sample["sr"]
        wm = part.find("SampleWarpProperties/WarpMarkers")
        if wm is not None and len(wm) >= 2:
            wm[1].set("SecTime", repr(dur))
            if len(wm) > 2:
                wm[2].set("SecTime", repr(dur + 0.02))
        fill_ref(part.find("SampleRef"), sample)
        parts.append(part)


def make_sampler_track(L, track, zones, ins, name):
    devs = track.find("DeviceChain/DeviceChain/Devices")
    drift = devs.find("Drift")
    sampler = load_sampler()
    L.renumber(sampler)
    sampler.set("Id", drift.get("Id"))
    devs.insert(list(devs).index(drift), sampler)
    devs.remove(drift)
    configure_sampler(sampler, zones, ins, L_samples.fill_ref, L_samples.use, name)


def make_simpler_track(L, track, sample, root_note, ins=None):
    devs = track.find("DeviceChain/DeviceChain/Devices")
    drift = devs.find("Drift")
    simpler = load_simpler()
    L.renumber(simpler)
    simpler.set("Id", drift.get("Id"))
    devs.insert(list(devs).index(drift), simpler)
    devs.remove(drift)
    configure_simpler(simpler, sample, root_note, ins, L_samples.fill_ref)


def configure_simpler(simpler, sample, root_note, ins, fill_ref):
    """El Simpler con su sample y los parámetros del tracker (para la pista
    exportada y para los presets .adv)."""
    # sonido neutro, como LGPT: sin filtro y con el sample sonando completo
    T.set_val(simpler, "UserName", sample["name"])
    for path in ("Pitch/TransposeKey", "Pitch/TransposeFine"):   # sin transposición (el modelo traía +12)
        if simpler.find(path) is not None:
            T.set_val(simpler, path + "/Manual", 0)
    flt = simpler.find("Filter")
    if flt is not None:
        for on in flt.iter("IsOn"):
            if on.find("Manual") is not None:
                T.set_val(on, "Manual", "false")
    vp = simpler.find("VolumeAndPan")
    if vp is not None:
        for name, val in (("SustainLevel", 1), ("ReleaseTime", 50), ("AttackTime", 0.1)):
            e = next(vp.iter(name), None)
            if e is not None and e.find("Manual") is not None:
                T.set_val(e, "Manual", val)
    part = next(simpler.iter("MultiSamplePart"))
    T.set_val(part, "Name", sample["name"])
    T.set_val(part, "Volume", 1)                     # el modelo traía +1.9 dB en el sample
    vol = int((ins or {}).get("volume", 128) or 128)  # instrumentos viejos (sin pantalla Sampler)
    _manual(simpler, "VolumeAndPan/Volume",
            round(SAMPLER_DB + (20 * math.log10(vol / 128.0) if vol > 0 else -36), 2))
    T.set_val(part, "RootKey", root_note)
    T.set_val(part, "SampleStart", 0)
    T.set_val(part, "SampleEnd", max(1, sample["frames"] - 1))
    dur = sample["frames"] / sample["sr"]
    wm = part.find("SampleWarpProperties/WarpMarkers")
    if wm is not None and len(wm) >= 2:
        wm[1].set("SecTime", repr(dur))
        if len(wm) > 2:
            wm[2].set("SecTime", repr(dur + 0.02))
    fill_ref(part.find("SampleRef"), sample)
    if ins is not None and ins.get("s init") not in (None, "0"):
        apply_sampler(simpler, part, ins)


def _manual(el, path, value):
    """Fija el valor Manual de un parámetro del Simpler, si existe en el modelo."""
    e = el.find(path)
    if e is not None and e.find("Manual") is not None:
        T.set_val(e, "Manual", value)


def apply_sampler(simpler, part, ins):
    """Pantalla Sampler del tracker -> Simpler de Live (mismos modos y secciones)."""
    def num(name, default=0):
        try:
            return int(ins.get(name, default))
        except (TypeError, ValueError):
            return default
    mode = {"classic": 0, "one-shot": 1, "slice": 2}.get(ins.get("s mode", "classic"), 0)
    start, end = num("s start"), num("s end", 100)
    end = max(start + 1, min(100, end))
    region = (end - start) / 100.0
    _manual(simpler, "Globals/PlaybackMode", mode)
    pm = simpler.find("Globals/PlaybackMode")
    if pm is not None and pm.find("Manual") is None:
        pm.set("Value", str(mode))
    # PLAYBACK (el tracker pone inicio, final y loop en cruces por cero: Snap)
    _manual(simpler, "Player/Snap", "true")
    # la región va en el sample mismo (inicio/final en muestras): Live la
    # respeta en Classic, 1-Shot y Slice; Start/Length del Simpler quedan
    # completos (en 1-Shot Live los ignoraba y tocaba el sample entero)
    frames = int(part.find("SampleEnd").get("Value")) + 1
    T.set_val(part, "SampleStart", round(frames * start / 100.0))
    T.set_val(part, "SampleEnd", max(1, round(frames * end / 100.0) - 1))
    _manual(simpler, "Player/LoopModulators/SampleStart", 0.0)
    _manual(simpler, "Player/LoopModulators/SampleLength", 1.0)
    loop = mode == 0 and ins.get("s loop") == "on"
    _manual(simpler, "Player/LoopModulators/LoopOn", "true" if loop else "false")
    # el tracker mide el loop sobre el sample entero; Live, sobre la región
    _manual(simpler, "Player/LoopModulators/LoopLength", min(1.0, num("s loop len", 100) / 100.0 / region))
    gate = 1 if ins.get("s trigger") == "gate" else 0
    _manual(simpler, "VolumeAndPan/OneShotEnvelope/SustainMode", gate)
    if mode == 2:
        marks = []
        for m in (ins.get("s marks") or "").split(","):
            try:
                marks.append(int(m))
            except ValueError:
                pass
        lo = int(part.find("SampleStart").get("Value"))
        hi = int(part.find("SampleEnd").get("Value"))
        marks = sorted(m for m in set(marks) if lo < m < hi)
        rate = part.find("SampleRef/DefaultSampleRate")
        rate = float(rate.get("Value")) if rate is not None else 44100.0
        if ins.get("s slice by") == "manual":
            # Slice by: Manual, con las marcas del tracker (en segundos del archivo)
            T.set_val(part, "SlicingStyle", 3)
            for tag in ("ManualSlicePoints", "SlicePoints"):
                el = part.find(tag)
                if el is None:
                    continue
                for c in list(el):
                    el.remove(c)
                for m in marks:
                    ET.SubElement(el, "SlicePoint", TimeInSeconds=repr(m / rate),
                                  Rank="0", NormalizedEnergy="1")
        else:
            T.set_val(part, "SlicingStyle", 2)              # por regiones: partes iguales
            T.set_val(part, "SlicingRegions", max(1, num("s slices", 4)))
        sl = simpler.find("SimplerSlicing/PlaybackMode")
        if sl is not None:
            sl.set("Value", "0")                              # mono, como el tracker
    # PITCH (C3 = altura original; la transposición ya incluye la raíz)
    T.set_val(part, "RootKey", 60)
    _manual(simpler, "Pitch/TransposeKey", max(-48, min(48, num("s transpose"))))
    _manual(simpler, "Pitch/TransposeFine", max(-50, min(50, num("s detune"))))
    glide = num("s glide")
    _manual(simpler, "Globals/PortamentoMode", 1 if glide > 0 else 0)
    _manual(simpler, "Globals/PortamentoTime", max(1, glide))
    nv = simpler.find("Globals/NumVoices")
    if nv is not None:                                        # 1 o 2 voces, como en el tracker
        nv.set("Value", str(max(1, min(2, num("s voices", 1))) - 1))
    # FILTER: el del Sampler es un 12 dB/oct en Hz, como el Clean de Simpler:
    # corte, resonancia y envolvente pasan tal cual
    ftype = {"LP": 0, "HP": 1, "BP": 2}.get(ins.get("s filter", "LP"), 0)
    if ins.get("s filter v") not in (None, "0"):
        hz, reso, fenv = num("s cutoff", 22000), num("s reso"), num("s fenv")
    else:                                   # instrumento de antes: misma conversión que el tracker
        f, r, e = num("s freq", 127), num("s res"), num("s env")
        hz, reso, fenv = int(30 * (22000 / 30.0) ** (f / 127.0) + 0.5), r * 100 // 127, round(e * 0.9)
    use_filter = not (ftype == 0 and hz >= 19000 and reso <= 0 and fenv <= 0 and num("s key") <= 0)
    flt = simpler.find("Filter")
    if flt is not None:
        _manual(flt, "IsOn", "true" if use_filter else "false")
        sf = flt.find("Slot/Value/SimplerFilter")
        if sf is not None:
            T.set_val(sf, "Type/Manual", ftype)
            _manual(sf, "Slope", "false")                      # 12 dB/oct
            T.set_val(sf, "Freq/Manual", max(30, min(22000, hz)))
            T.set_val(sf, "Res/Manual", round(max(0, min(100, reso)) / 100.0, 3))
            # key tracking (100 % = un semitono por semitono) y velocity
            _manual(sf, "ModByPitch", round(max(0, min(100, num("s key"))) / 100.0, 3))
            _manual(sf, "ModByVelocity", round(max(0, min(100, num("s f velocity"))) / 100.0, 3))
            fe = sf.find("Envelope")
            if fe is not None:
                if ins.get("s fenv v") not in (None, "0"):     # envolvente propia del filtro
                    fa, fd, fs_db, fr = (num("s f attack"), num("s f decay", 600),
                                         num("s f sustain"), num("s f release", 50))
                elif mode == 1:                                 # antes: seguía a la de volumen
                    fa, fd, fs_db, fr = num("s fade in"), 0, 0, num("s fade out")
                else:
                    fa, fd, fs_db, fr = (num("s attack"), num("s decay", 600),
                                         num("s sustain"), num("s release", 50))
                _manual(fe, "IsOn", "true" if fenv else "false")
                _manual(fe, "Amount", max(-72, min(72, fenv)))
                for name, val in (("AttackTime", max(0.1, fa)), ("DecayTime", max(1, fd)),
                                  ("SustainLevel", round(0 if fs_db <= -60 else 10 ** (fs_db / 20.0), 4)),
                                  ("ReleaseTime", max(1, fr))):
                    _manual(fe, name, val)
    # AMP
    _manual(simpler, "VolumeAndPan/Volume", round(max(-36, min(36, SAMPLER_DB + num("s volume"))), 2))
    _manual(simpler, "VolumeAndPan/Panorama", max(-1.0, min(1.0, num("s pan") / 50.0)))
    # el velocity del piano roll cambia el volumen en Chancho 6: en Live también
    _manual(simpler, "VolumeAndPan/VolumeVelScale", 1)
    sus = num("s sustain")
    env_amp = (("AttackTime", max(0.1, num("s attack"))), ("DecayTime", max(1, num("s decay", 600))),
               ("SustainLevel", 0 if sus <= -60 else 10 ** (sus / 20.0)), ("ReleaseTime", max(1, num("s release", 50))))
    for name, val in env_amp:
        _manual(simpler, "VolumeAndPan/Envelope/" + name, val)
    _manual(simpler, "VolumeAndPan/OneShotEnvelope/FadeInTime", num("s fade in"))
    _manual(simpler, "VolumeAndPan/OneShotEnvelope/FadeOutTime", num("s fade out"))
    # WARP: off / re-pitch / complex, con el tempo del sample
    warp = {"re-pitch": 3, "complex": 4}.get(ins.get("s warp", "off"))
    swp = part.find("SampleWarpProperties")
    if swp is not None:
        T.set_val(swp, "IsWarped", "true" if warp else "false")
        if warp:
            T.set_val(swp, "WarpMode", warp)
            bpm = num("s bpm", 120) or 120
            wm = swp.find("WarpMarkers")
            if wm is not None and len(wm) >= 2:
                dur = float(wm[1].get("SecTime"))
                beats = dur * bpm / 60.0
                wm[1].set("BeatTime", f"{beats:g}")
                if len(wm) > 2:
                    wm[2].set("SecTime", repr(dur + 0.02))
                    wm[2].set("BeatTime", f"{beats + 0.02 * bpm / 60.0:g}")


def ensure_scenes(L, n):
    scenes = L.ls.find("Scenes")
    lists = [l for t in list(L.tracks_el) + [L.ls.find("MainTrack")]
             for l in t.iter("ClipSlotList") if len(l)]
    while len(scenes) < n:
        sc = copy.deepcopy(scenes[-1])
        sc.set("Id", str(max(int(s.get("Id")) for s in scenes) + 1))
        scenes.append(sc)
    for l in lists:
        while len(l) < n:
            slot = copy.deepcopy(l[-1])
            v = slot.find("ClipSlot/Value")
            if v is not None:
                for c in list(v):
                    v.remove(c)
            slot.set("Id", str(len(l)))
            l.append(slot)


L_samples = None


def convert(folder, template, out, real_dir=None):
    global L_samples
    lg = LGPT(folder)
    L = T.LiveSet(template)
    proj_dir = os.path.dirname(os.path.abspath(out))
    L_samples = T.Samples(os.path.join(folder, "samples"), proj_dir, real_dir or proj_dir)
    proto_midi = next(L.proto["DRUMS"].iter("MidiClip"))
    proto_audio = next(L.proto["LOOP"].iter("AudioClip"))
    stats = Counter()

    # tempo
    tempo = L.ls.find("MainTrack/DeviceChain/Mixer/Tempo")
    T.set_val(tempo, "Manual", lg.tempo)
    for env in L.ls.findall("MainTrack/AutomationEnvelopes/Envelopes/AutomationEnvelope"):
        if env.find("EnvelopeTarget/PointeeId").get("Value") == T.target_id(tempo):
            for e in env.find("Automation/Events"):
                e.set("Value", str(lg.tempo))

    last = lg.last_row()
    if last < 0:
        sys.exit("la song está vacía")
    n_scenes = last + 1

    # 1) recorrer la song: clips por canal y fila, e instrumentos usados por canal
    clips, used = {}, {ch: Counter() for ch in range(CHANNELS)}
    for ch in range(CHANNELS):
        last_instr = [None]
        for row in range(n_scenes):
            c = lg.cell(row, ch)
            if lg.is_audio(ch):                  # celda = instrumento con el loop
                if c != EMPTY and lg.instruments.get(c, {}).get("type") == "Sample":
                    clips[(ch, row)] = ("audio", c, None, lg.clip_bars(c) * 4)
                    used[ch][c] += 1
                continue
            if lg.clip_model:                    # Chancho 6: clips propios
                if c == EMPTY or c not in lg.clips or lg.owner[ch] is None:
                    continue
                notes = sorted(clip_notes(lg, c, lg.owner[ch]), key=lambda n: (n[0], n[2]))
                clips[(ch, row)] = (c, notes, [], lg.clips[c]["pages"] * T.PHRASE_LEN)
                for e in notes:
                    used[ch][e[3]] += 1
                if not notes:
                    used[ch][lg.owner[ch]] += 1     # clip vacío: la pista igual lleva su instrumento
                continue
            if c == EMPTY or not lg.chain_phrases(c):
                continue
            ev, kills, cmds, length = flatten_chain(lg, c, last_instr, stats, lg.owner[ch])
            notes = durations(ev, kills, length) + voice_notes(lg, c, last_instr[0])
            notes = apply_timing(notes, note_timing(lg, c))
            notes.sort(key=lambda n: (n[0], n[2]))
            clips[(ch, row)] = (c, notes, cmds, length)
            for e in notes:
                used[ch][e[3]] += 1

    # 2) pistas
    returns = [t for t in L.tracks_el if t.tag == "ReturnTrack"]
    for t in list(L.tracks_el):
        L.tracks_el.remove(t)
    built = {}
    for ch in range(CHANNELS):
        if lg.types[ch] == "OFF":                   # pista que no existe en el tracker
            continue
        instrs = [i for i, _ in used[ch].most_common()]
        samples = [i for i in instrs if lg.instruments.get(i, {}).get("type") == "Sample"
                   and lg.instruments[i].get("sample")]
        loops = [i for i in samples if lg.instruments[i].get("loopmode") == "looper sync"]
        if lg.is_audio(ch) and samples:
            kind, name = "audio", os.path.splitext(lg.instruments[samples[0]]["sample"])[0]
        elif not samples:
            kind, name = "drift", "MIDI"            # pista vacía: sin instrumento
        elif loops and len(loops) == len(samples):
            kind, name = "audio", os.path.splitext(lg.instruments[loops[0]]["sample"])[0]
        elif len(samples) == 1 and lg.instruments[samples[0]].get("s kit") == "1" and parse_zones(lg.instruments[samples[0]]):
            kind, name = "kit", os.path.splitext(lg.instruments[samples[0]]["sample"])[0]   # kit de Chancho 6
        elif len(samples) == 1:
            kind, name = "simpler", os.path.splitext(lg.instruments[samples[0]]["sample"])[0]
        else:
            kind, name = "kit", f"CH{ch} KIT"
        if lg.names[ch]:                             # el nombre de la pista (el del preset)
            name = lg.names[ch]
        color = T.COLORS[kind if kind in ("kit", "audio") else "drift"]
        tr = L.clone_track("drift" if kind == "simpler" else kind, f"{ch + 1} {name}"[:30],
                           color[ch % len(color) if kind == "audio" else ch % 2])
        pcvm = tr.find("PreferredContentViewMode")
        if pcvm is not None:
            pcvm.set("Value", "0")                   # el clip abre en sus notas (1 = envolventes)
        fader = tr.find("DeviceChain/Mixer/Volume/Manual")
        if fader is not None:
            fader.set("Value", "1")                  # 0 dB: el nivel va en el clip / el instrumento
        padmap = {}
        if kind == "drift" and not samples:
            devs = tr.find("DeviceChain/DeviceChain/Devices")
            if devs is not None:
                for d in list(devs):
                    devs.remove(d)
        if kind == "simpler":
            ins = lg.instruments[samples[0]]
            zones = parse_zones(ins)
            if zones:                                # soundfont: Sampler con sus zonas
                make_sampler_track(L, tr, zones, ins, re.sub(r"_\d+$", "", name))
            else:
                make_simpler_track(L, tr, L_samples.use(ins["sample"]), int(ins.get("root note", 60)), ins)
        elif kind == "kit" and lg.instruments[samples[0]].get("s kit") == "1":
            # kit de Chancho 6: cada zona es un pad en su misma tecla
            ins = lg.instruments[samples[0]]
            pads, pad_db = {}, {}
            pparams = parse_pads(ins)
            for lo, hi, root, cents, ls, le, wav in parse_zones(ins):
                pads[lo] = wav
                pad_db[lo] = round(SAMPLER_DB + sampler_db(ins) + pparams.get(lo, {}).get("gain", 0), 2)
            padmap = None                            # las notas ya son los pads
            T.build_kit(L, tr, pads, L_samples, pad_db, pparams)
        elif kind == "kit":
            pads, pad_db = {}, {}
            for k, i in enumerate(samples[:16]):
                pads[36 + k] = lg.instruments[i]["sample"]
                padmap[i] = 36 + k
                pad_db[36 + k] = round(SAMPLER_DB + sampler_db(lg.instruments[i]), 2)   # como suena en el tracker
            if len(samples) > 16:
                print(f"  aviso: canal {ch} usa {len(samples)} instrumentos; solo caben 16 pads")
            T.build_kit(L, tr, pads, L_samples, pad_db)
        for h in tr.findall("DeviceChain/Mixer/Sends/TrackSendHolder"):   # LGPT no tiene envíos
            h.find("Send/Manual").set("Value", str(T.SEND_MIN))
        L.tracks_el.append(tr)
        built[ch] = (tr, kind, padmap)
    for r in returns:
        L.tracks_el.append(r)

    ensure_scenes(L, n_scenes)

    # 3) clips
    session = {}
    for (ch, row), (chain, notes, cmds, length) in sorted(clips.items()):
        tr, kind, padmap = built[ch]
        if chain == "audio":                      # celda de un canal de audio del fork
            ins = notes
            ac = audio_clip(lg.instruments[ins], proto_audio, row, lg.loop_bars(ins) * 4,
                            os.path.splitext(lg.instruments[ins].get("sample", ""))[0] or f"A{ins:02X}")
            T.set_val(ac, "Color", tr.find("Color").get("Value"))
            T.slots(tr)[row].find("ClipSlot/Value").append(ac)
            session[(ch, row)] = (ac, length)
            continue
        if kind == "audio" and notes:
            ac = loop_clip(lg, proto_audio, row, chain, notes, length, ch)
            T.set_val(ac, "Color", tr.find("Color").get("Value"))
            T.slots(tr)[row].find("ClipSlot/Value").append(ac)
            session[(ch, row)] = (ac, length)
            continue
        if kind == "drift" or not notes:
            continue                                  # canal sin instrumento o chain de silencio
        out_notes = []
        starts = {n[0] for n in notes}
        for t, d, note, ins, vel in notes:
            sound = sample_beats(lg, ins, note)
            info = lg.instruments.get(ins, {})
            # glide: en el tracker la nota se desliza a la siguiente si la
            # anterior sigue sonando; el Glide de Simpler (mono) solo se
            # desliza si las notas se enciman, así que se enciman un poco
            glide = (kind == "simpler" and info.get("s init") not in (None, "0")
                     and info.get("s mode", "classic") == "classic"
                     and int(info.get("s glide", 0) or 0) > 0
                     and int(info.get("s voices", 1) or 1) < 2)
            # (solo si el sample de verdad sigue sonando: si se acaba antes, en
            # Live la nota encimada no lo vuelve a disparar y se queda callado)
            if glide and (t + d) in starts and sample_beats(lg, ins, note, exact=True) >= d + T.STEP / 4:
                d = d + T.STEP / 4
            else:
                d = min(d, sound)
            if kind == "kit" and padmap is None:     # kit por zonas: la nota es el pad
                out_notes.append((t, d, max(0, min(127, note)), vel))
            elif kind == "kit":
                if ins not in padmap:
                    continue
                out_notes.append((t, d, padmap[ins], vel))
            else:
                out_notes.append((t, d, max(0, min(127, note)), vel))
        clip = copy.deepcopy(proto_midi)
        clip.set("Id", str(row))
        clip.find("Name").set("Value", lg.chain_names.get(chain) or f"{chain:02X}")
        T.set_val(clip, "Color", tr.find("Color").get("Value"))
        for p in ("CurrentEnd", "Loop/LoopEnd", "Loop/OutMarker", "Loop/HiddenLoopEnd"):
            T.set_val(clip, p, f"{length:g}")
        if chain in lg.chain_loops:               # el loop del clip (como en el Move)
            a, z = lg.chain_loops[chain]
            a, z = a * T.PHRASE_LEN, min(length, (z + 1) * T.PHRASE_LEN)
            if a < z:
                for p in ("CurrentStart", "Loop/LoopStart", "Loop/HiddenLoopStart"):
                    T.set_val(clip, p, f"{a:g}")
                for p in ("CurrentEnd", "Loop/LoopEnd", "Loop/HiddenLoopEnd"):
                    T.set_val(clip, p, f"{z:g}")
                T.set_val(clip, "Loop/StartRelative", "0")
                length = z - a
        envs = clip.find("Envelopes/Envelopes")
        for e in list(envs):
            envs.remove(e)
        T.write_notes(clip, out_notes)
        mixer = tr.find("DeviceChain/Mixer")
        for letter in sorted({c for _, c, _ in cmds}):
            if letter == "P":
                par = mixer.find("Pan")
            else:
                af = L.ensure_autofilter(tr)
                par = af.find("Filter_Frequency" if letter == "F" else "Filter_Resonance")
            changes = [(t, T.cmd_value(letter, v)) for t, c, v in cmds if c == letter]
            T.write_envelope(clip, T.target_id(par), float(par.find("Manual").get("Value")), changes)
        T.slots(tr)[row].find("ClipSlot/Value").append(clip)
        session[(ch, row)] = (clip, length)

    for i, sc in enumerate(L.ls.findall("Scenes/Scene")):
        sc.find("Name").set("Value", f"FILA {i:02X}")

    # 4) Arrangement: mismas reglas que LGPT (ver trk2als.arrange_sections)
    items = dict(session)
    for (ch, row), (chain, notes, cmds, length) in clips.items():
        if (ch, row) not in items and built[ch][1] != "drift":
            items[(ch, row)] = (None, length)        # chain sin notas (p. ej. 7F) = silencio
    kinds = {ch: v[:2] for ch, v in built.items() if v[1] != "drift"}
    n_sec, t = T.arrange_sections(T.Arrangement(), kinds, items, range(n_scenes))
    print(f"  arreglo: {n_sec} secciones, {t / 4:g} compases")

    T.check(L.root)
    apply_scale(L, lg.scale, lg.scale_root)
    L.save(out)
    print(f"ok → {out}: {lg.tempo:g} BPM, {n_scenes} escenas, {len(clips)} clips")
    for ch in sorted(built):
        tr, kind, padmap = built[ch]
        label = "vacía" if kind == "drift" else kind
        print(f"  canal {ch}: {label:7s} {tr.find('Name/EffectiveName').get('Value')}")
    if n_scenes > 8:
        print(f"  aviso: {n_scenes} escenas — Live Lite/Intro solo abren 8 sin modo demo")
    if stats:
        print("  comandos no convertidos todavía:", ", ".join(f"{k.strip()}×{v}" for k, v in stats.most_common()))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--real-dir=")]
    real = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--real-dir=")), None)
    if len(args) != 3:
        sys.exit(__doc__)
    convert(args[0], args[1], args[2], real)
