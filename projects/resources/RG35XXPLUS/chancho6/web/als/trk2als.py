"""trk2als — convierte un proyecto .trk del tracker en un Live Set (.als).

Uso:  python3 trk2als.py proyecto.trk plantilla.als salida.als [--real-dir=/ruta/del/proyecto]

Los samples usados se copian a <carpeta de salida>/Samples/libreria/.

La plantilla es un .als guardado desde Live con tres pistas prototipo:
  DRUMS (Drum Rack con Drum Samplers), BASS (Drift + Auto Filter), LOOP (audio)
y dos retornos (A = Reverb, B = Delay). Cada pista del .trk se clona de su prototipo.
"""
import copy, gzip, math, os, re, shutil, sys, wave
import xml.etree.ElementTree as ET

STEP = 0.25              # un paso = 1/16 = 0.25 tiempos
PHRASE_LEN = 16 * STEP   # una phrase = 1 compás
SEND_MIN = 0.0003162277571
PROTO = {"kit": "DRUMS", "drift": "BASS", "audio": "LOOP"}
COLORS = {"kit": [16, 17], "drift": [15, 13, 14], "audio": [1, 2, 3]}
NOTE_RE = re.compile(r"^([A-G])(#?)(-?\d)$")
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


# ───────────────────────────── lectura del .trk ─────────────────────────────
class Project:
    def __init__(self):
        self.tempo = 120
        self.library = None  # carpeta de samples (relativa al .trk)
        self.tracks = {}     # n -> dict(name, type, sends)
        self.pads = {}       # n pista -> {nota: ruta en la librería}
        self.clips = []      # dict(track, scene, chain=[(phrase, transpose)] | ("loop", ruta))
        self.phrases = {}    # id -> list of rows
        self.song = []       # [(escena, repeticiones)] -> Arrangement


def note_value(tok):
    if tok == "OFF":
        return "OFF"
    m = NOTE_RE.match(tok)          # nombres como en Live: C1 = 36, C3 = 60
    if not m:
        raise ValueError("nota inválida: " + tok)
    letter, sharp, octv = m.groups()
    return 12 * (int(octv) + 2) + SEMI[letter] + (1 if sharp else 0)


def parse_db(tok):
    return None if tok.lower() == "off" else float(tok)


def parse(path):
    p, cur = Project(), None
    for ln, raw in enumerate(open(path, encoding="utf-8"), 1):
        # comentarios: líneas que empiezan con '#', o ' # ...' al final (F#1 no es comentario)
        line = "" if raw.lstrip().startswith("#") else re.split(r"\s#(?:\s|$)", raw)[0].strip()
        if not line:
            continue
        t = line.split()
        try:
            if t[0] == "tempo":
                p.tempo = float(t[1]); cur = None
            elif t[0] == "library":
                p.library = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(path)),
                                                          line.split(None, 1)[1]))
                cur = None
            elif t[0] == "pad":
                _, n, note, rest = line.split(None, 3)
                p.pads.setdefault(int(n), {})[note_value(note)] = rest.strip()
                cur = None
            elif t[0] == "track":
                p.tracks[int(t[1])] = dict(name=t[2], type=t[3], sends=(parse_db(t[4]), parse_db(t[5])))
                cur = None
            elif t[0] == "clip":
                chain = []
                if len(t) > 3 and t[3].startswith("--"):
                    chain = ("rest", float(t[3][2:] or 1) * 4)      # --N = N compases de silencio
                elif len(t) > 3 and t[3] == "loop":
                    parts = line.split(None, 4)
                    if len(parts) < 5:
                        raise ValueError("'loop' necesita la ruta del archivo en la librería")
                    chain = ("loop", parts[4].strip())
                for c in ([] if chain else t[3:]):
                    ph, _, tr = c.partition("+")
                    if "-" in ph:
                        ph, tr = ph.split("-"); tr = "-" + tr
                    chain.append((ph.upper(), int(tr or 0)))
                p.clips.append(dict(track=int(t[1]), scene=int(t[2]), chain=chain))
                cur = None
            elif t[0] == "song":
                for tok in t[1:]:
                    sc, _, rep = tok.lower().partition("x")
                    p.song.append((int(sc), int(rep or 1)))
                cur = None
            elif t[0] == "phrase":
                cur = t[1].upper(); p.phrases[cur] = []
            elif cur is not None:
                row = dict(step=int(t[0], 16), note=note_value(t[1]), vel=100, cmds=[])
                for tok in t[2:]:
                    if tok == "-":
                        continue
                    if len(tok) == 2:
                        row["vel"] = max(1, min(127, int(tok, 16)))
                    elif len(tok) == 3 and tok[0].isalpha():
                        row["cmds"].append((tok[0].upper(), int(tok[1:], 16)))
                    else:
                        raise ValueError("token desconocido: " + tok)
                p.phrases[cur].append(row)
            else:
                raise ValueError("línea fuera de lugar")
        except Exception as e:
            sys.exit(f"{path}:{ln}: {e}\n  {raw.rstrip()}")
    return p


# ─────────────────────── de comandos a valores de Live ───────────────────────
def db_to_gain(db):
    return SEND_MIN if db is None else 10 ** (db / 20)


def cmd_value(letter, v):
    if letter in "ABV":                 # 00 = apagado, FF = 0 dB
        return SEND_MIN if v == 0 else db_to_gain(-60 + 60 * v / 255)
    if letter == "P":                   # 00 izquierda, 80 centro, FF derecha
        return max(-1.0, min(1.0, (v - 128) / 127))
    if letter == "F":                   # 20 Hz – 20 kHz, logarítmico
        return 20 * 1000 ** (v / 255)
    if letter == "Q":
        return v / 255
    raise ValueError("comando no soportado: " + letter)


# ───────────────────────────── edición del XML ──────────────────────────────
POINTEE_TAGS = ("Target", "Pointee")


class LiveSet:
    def __init__(self, path):
        with gzip.open(path, "rb") as f:
            self.root = ET.fromstring(f.read())
        self.ls = self.root.find("LiveSet")
        self.tracks_el = self.ls.find("Tracks")
        self.proto = {t.find("Name/EffectiveName").get("Value"): t for t in self.tracks_el}
        self.next_pointee = int(self.ls.find("NextPointeeId").get("Value"))
        self.next_track_id = max(int(t.get("Id")) for t in self.tracks_el) + 1
        self.autofilter = next(self.proto["BASS"].iter("AutoFilter2"))

    def new_pointee(self):
        self.next_pointee += 1
        return self.next_pointee - 1

    def renumber(self, el):
        """Da Ids nuevos a todos los parámetros automatizables de un elemento clonado."""
        remap = {}
        for e in el.iter():
            if e.get("Id") is not None and (e.tag.endswith(POINTEE_TAGS) or e.tag.startswith("ControllerTargets")):
                new = str(self.new_pointee())
                remap[e.get("Id")] = new
                e.set("Id", new)
        for e in el.iter("PointeeId"):
            if e.get("Value") in remap:
                e.set("Value", remap[e.get("Value")])

    def clone_track(self, kind, name, color):
        t = copy.deepcopy(self.proto[PROTO[kind]])
        self.renumber(t)
        t.set("Id", str(self.next_track_id)); self.next_track_id += 1
        for p in ("Name/EffectiveName", "Name/UserName"):
            t.find(p).set("Value", name)
        t.find("Color").set("Value", str(color))
        for armed in t.iter("IsArmed"):           # ninguna pista armada para grabar
            armed.set("Value", "false")
        devs = t.find("DeviceChain/DeviceChain/Devices")
        if devs is not None:                      # el Auto Filter solo se agrega si F/Q lo usan
            for af in devs.findall("AutoFilter2"):
                devs.remove(af)
        for s in slots(t):                      # vaciar clips del prototipo
            v = s.find("ClipSlot/Value")
            for c in list(v):
                v.remove(c)
        return t

    def ensure_autofilter(self, track):
        devs = track.find("DeviceChain/DeviceChain/Devices")
        af = devs.find("AutoFilter2")
        if af is None:
            af = copy.deepcopy(self.autofilter)
            self.renumber(af)
            af.set("Id", str(max([int(d.get("Id")) for d in devs] + [-1]) + 1))
            devs.append(af)
        return af

    def save(self, path):
        self.ls.find("NextPointeeId").set("Value", str(self.next_pointee))
        # abrir el clip en las notas, sin el panel de envolventes desplegado
        for tag in ("IsExpressionSplitterOpen",):
            e = self.ls.find(tag)
            if e is not None:
                e.set("Value", "false")
        # zoom del editor de notas: el alto de tecla que le gusta al usuario
        # (guardado a mano en Live: 1605 / -690; Live encuadra las notas con
        # ese mismo desplazamiento aunque estén más arriba o abajo). En los
        # otros modos, -1 / 0 = "sin guardar"
        for clip in self.root.iter("MidiClip"):
            for tag, val in (("NoteEditorFoldInZoom", "-1"), ("NoteEditorFoldInScroll", "0"),
                             ("NoteEditorFoldOutZoom", "1605"), ("NoteEditorFoldOutScroll", "-690"),
                             ("NoteEditorFoldScaleZoom", "-1"), ("NoteEditorFoldScaleScroll", "0"),
                             ("ScrollerTimePreserver/LeftTime", "0"),
                             ("ScrollerTimePreserver/RightTime", "0")):
                e = clip.find(tag)
                if e is not None:
                    e.set("Value", val)
        data =ET.tostring(self.root, encoding="UTF-8", xml_declaration=True)
        with gzip.open(path, "wb") as f:
            f.write(data)


def slots(track):
    return track.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot")


def target_id(el):
    return el.find("AutomationTarget").get("Id")


def set_val(el, path, value):
    el.find(path).set("Value", str(value))


# ──────────────────────────────── samples ────────────────────────────────────
def wav_info(path):
    """(cuadros, frecuencia) leyendo la cabecera: sirve para PCM de 8/16/24/32
    bits y float de 32 (el módulo wave solo entiende PCM)"""
    import struct
    with open(path, "rb") as f:
        if f.read(4) != b"RIFF":
            raise ValueError(f"{path}: no es WAV")
        f.read(4)
        if f.read(4) != b"WAVE":
            raise ValueError(f"{path}: no es WAV")
        sr = align = None
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                raise ValueError(f"{path}: sin datos")
            cid, size = hdr[:4], struct.unpack("<I", hdr[4:])[0]
            if cid == b"fmt ":
                fmt = f.read(size)
                _, ch, sr, _, align, bits = struct.unpack("<HHIIHH", fmt[:16])
            elif cid == b"data":
                return size // align, sr
            else:
                f.seek(size + (size & 1), 1)


def loop_tempo(path, frames, sr):
    """BPM y compases de un loop: usa el BPM del nombre si lo hay; si no, lo adivina."""
    dur = frames / sr
    name = os.path.splitext(os.path.basename(path))[0]
    m = re.search(r"(\d{2,3})\s*bpm", name, re.I)
    cands = [int(m.group(1))] if m else [int(x) for x in re.split(r"[\s_\-]+", name)
                                         if x.isdigit() and 60 <= int(x) <= 200]
    for bpm in cands:
        bars = dur * bpm / 60 / 4
        if bars >= 0.5 and abs(bars - round(bars)) < 0.05 * max(1, round(bars)):
            return bpm, max(1, round(bars))
    for bars in (1, 2, 4, 8, 16, 32):                 # sin pista: el que dé 80–160 BPM
        bpm = bars * 4 * 60 / dur
        if 80 <= bpm <= 160:
            return round(bpm, 2), bars
    return round(4 * 60 / dur, 2), 1


class Samples:
    """Copia los samples usados a <proyecto>/Samples/libreria/ y llena las referencias de Live."""

    def __init__(self, library, project_dir, real_project_dir, refs=None):
        self.library, self.project_dir, self.real_dir = library, project_dir, real_project_dir
        self.refs = refs or {}   # trk2als: nombre -> ruta (samples que el proyecto no copió)
        # sin esta carpeta Live no trata el folder como proyecto y no resuelve las rutas relativas
        os.makedirs(os.path.join(project_dir, "Ableton Project Info"), exist_ok=True)

    def use(self, rel):
        if not self.library:
            sys.exit("el proyecto usa samples: falta la línea 'library <carpeta>'")
        src = os.path.join(self.library, rel)
        if not os.path.isfile(src) and rel in self.refs:
            src = self.refs[rel]
        if not os.path.isfile(src):
            sys.exit(f"no encuentro el sample: {src}")
        live_rel = "Samples/libreria/" + rel.replace(os.sep, "/")
        dst = os.path.join(self.project_dir, *live_rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
            shutil.copy2(src, dst)
        frames, sr = wav_info(dst)
        return dict(src=src, dst=dst, live_rel=live_rel, frames=frames, sr=sr,
                    size=os.path.getsize(dst), mtime=int(os.path.getmtime(dst)),
                    name=os.path.splitext(os.path.basename(rel))[0])

    def fill_ref(self, sample_ref, s):
        fr = sample_ref.find("FileRef")
        set_val(fr, "RelativePathType", 3)                       # relativo al proyecto
        set_val(fr, "RelativePath", s["live_rel"])
        set_val(fr, "Path", self.real_dir.rstrip("/") + "/" + s["live_rel"])
        set_val(fr, "OriginalFileSize", s["size"])
        set_val(fr, "OriginalCrc", 0)
        set_val(sample_ref, "LastModDate", s["mtime"])
        sc = sample_ref.find("SourceContext")
        for c in list(sc):
            sc.remove(c)
        if sample_ref.find("DefaultDuration") is not None:
            set_val(sample_ref, "DefaultDuration", s["frames"])
            set_val(sample_ref, "DefaultSampleRate", s["sr"])


def build_kit(L, track, pads, samples, pad_db=None, pad_params=None):
    """Rehace el Drum Rack: un pad con Drum Sampler por cada nota definida en el .trk.
    pad_db: {nota: volumen en dB del Drum Sampler} (si no, se queda el del modelo).
    pad_params: {nota: dict(tune, pan, decay, choke, start)} del editor de kits de Chancho 6."""
    branches = next(track.iter("DrumGroupDevice")).find("Branches")
    proto = next(b for b in branches if b.find(".//DrumCell") is not None)
    for b in list(branches):
        branches.remove(b)
    for i, (note, rel) in enumerate(sorted(pads.items())):
        s = samples.use(rel)
        b = copy.deepcopy(proto)
        L.renumber(b)
        b.set("Id", str(i))
        set_val(b, "BranchInfo/ReceivingNote", 128 - note)       # Live guarda 128 - nota
        for p in ("Name/EffectiveName", "Name/UserName"):
            set_val(b, p, s["name"])
        dc = b.find(".//DrumCell")
        samples.fill_ref(dc.find("UserSample/Value/SampleRef"), s)
        if pad_db and note in pad_db:
            for tag, val in (("Volume", pad_db[note]), ("Voice_VelocityToVolume", 1)):
                e = dc.find(tag + "/Manual")
                if e is not None:
                    e.set("Value", str(val))
        pp = (pad_params or {}).get(note)
        if pp:
            vals = [("Voice_Transpose", pp["tune"]), ("Pan", round(pp["pan"] / 50, 4)),
                    ("Voice_PlaybackStart", round(pp["start"] / 100, 4))]
            if pp["decay"] > 0:          # el golpe se apaga en ese tiempo (sin hold)
                vals += [("Voice_Envelope_Hold", 0.001), ("Voice_Envelope_Decay", round(pp["decay"] / 1000, 4))]
            for tag, val in vals:
                e = dc.find(tag + "/Manual")
                if e is not None:
                    e.set("Value", str(val))
            set_val(b, "BranchInfo/ChokeGroup", pp["choke"])
        branches.append(b)


def build_audio_clip(proto_audio, cid, rel, samples):
    s = samples.use(rel)
    bpm, bars = loop_tempo(s["src"], s["frames"], s["sr"])
    beats, dur = bars * 4, s["frames"] / s["sr"]
    clip = copy.deepcopy(proto_audio)
    clip.set("Id", str(cid))
    set_val(clip, "Name", s["name"])
    samples.fill_ref(clip.find("SampleRef"), s)
    for p in ("CurrentEnd", "Loop/LoopEnd", "Loop/OutMarker", "Loop/HiddenLoopEnd"):
        set_val(clip, p, beats)
    for p in ("CurrentStart", "Loop/LoopStart", "Loop/StartRelative", "Loop/HiddenLoopStart"):
        set_val(clip, p, 0)
    set_val(clip, "IsWarped", "true")
    set_val(clip, "WarpMode", 0 if "percussion" in rel.lower() or "drum" in rel.lower() else 4)
    wm = clip.find("WarpMarkers")
    for w in list(wm):
        wm.remove(w)
    spb = dur / beats
    for i, (sec, beat) in enumerate([(0, 0), (dur, beats), (dur + spb / 32, beats + 1 / 32)]):
        ET.SubElement(wm, "WarpMarker", Id=str(i), SecTime=repr(sec), BeatTime=f"{beat:g}")
    print(f"  loop {s['name']}: {bpm:g} BPM, {bars} compases")
    return clip


# ─────────────────────────────── exportación ────────────────────────────────
def flatten(project, chain, kind):
    """Une las phrases de la cadena: devuelve notas [(t, dur, key, vel)], comandos y largo."""
    hits, cmds = [], []
    for i, (ph, tr) in enumerate(chain):
        if ph not in project.phrases:
            sys.exit(f"la phrase {ph} no existe")
        base = i * PHRASE_LEN
        for r in project.phrases[ph]:
            t = base + r["step"] * STEP
            hits.append((t, r["note"] if r["note"] == "OFF" else r["note"] + tr, r["vel"]))
            cmds += [(t, c, v) for c, v in r["cmds"]]
    length = len(chain) * PHRASE_LEN
    hits.sort(key=lambda h: h[0])
    notes = []
    for i, (t, key, vel) in enumerate(hits):
        if key == "OFF":
            continue
        if kind == "kit":
            dur = STEP
        else:                                   # monofónico: dura hasta la siguiente nota u OFF
            nxt = next((h[0] for h in hits[i + 1:] if h[0] > t), length)
            dur = nxt - t
        notes.append((t, dur, key, vel))
    return notes, sorted(cmds), length


def write_notes(clip, notes):
    nt = clip.find("Notes")
    kts = nt.find("KeyTracks")
    for kt in list(kts):
        kts.remove(kt)
    by_key = {}
    for t, d, k, v in notes:
        by_key.setdefault(k, []).append((t, d, v))
    nid = 1
    for i, k in enumerate(sorted(by_key)):
        kt = ET.SubElement(kts, "KeyTrack", Id=str(i))
        n = ET.SubElement(kt, "Notes")
        for t, d, v in by_key[k]:
            ET.SubElement(n, "MidiNoteEvent", Time=f"{t:g}", Duration=f"{d:g}", Velocity=str(v),
                          OffVelocity="64", NoteId=str(nid))
            nid += 1
        ET.SubElement(kt, "MidiKey", Value=str(k))
    set_val(nt, "NoteIdGenerator/NextId", nid)


def write_envelope(clip, pointee, default, changes):
    """changes: [(t, valor)] en estilo tracker: el valor se mantiene hasta el siguiente comando."""
    envs = clip.find("Envelopes/Envelopes")
    ce = ET.SubElement(envs, "ClipEnvelope", Id=str(len(envs)))
    ET.SubElement(ET.SubElement(ce, "EnvelopeTarget"), "PointeeId", Value=str(pointee))
    auto = ET.SubElement(ce, "Automation")
    ev = ET.SubElement(auto, "Events")
    pts, prev = [(-63072000, default)], default
    for t, v in changes:
        if t > 0:
            pts.append((t, prev))           # escalón vertical, como en un tracker
        pts.append((t, v)); prev = v
    for i, (t, v) in enumerate(pts):
        ET.SubElement(ev, "FloatEvent", Id=str(i), Time=f"{t:g}", Value=f"{v:.6g}")
    vs = ET.SubElement(auto, "AutomationTransformViewState")
    ET.SubElement(vs, "IsTransformPending", Value="false")
    ET.SubElement(vs, "TimeAndValueTransforms")


def export(project, template, out, real_dir=None):
    L = LiveSet(template)
    proj_dir = os.path.dirname(os.path.abspath(out))
    samples = Samples(project.library, proj_dir, real_dir or proj_dir)
    proto_midi = next(L.proto["DRUMS"].iter("MidiClip"))
    proto_audio = next(L.proto["LOOP"].iter("AudioClip"))

    # tempo (perilla + punto base de automatización)
    tempo = L.ls.find("MainTrack/DeviceChain/Mixer/Tempo")
    set_val(tempo, "Manual", project.tempo)
    for env in L.ls.findall("MainTrack/AutomationEnvelopes/Envelopes/AutomationEnvelope"):
        if env.find("EnvelopeTarget/PointeeId").get("Value") == target_id(tempo):
            for e in env.find("Automation/Events"):
                e.set("Value", str(project.tempo))

    # pistas nuevas, en orden, antes de los retornos
    built, count = {}, {}
    returns = [t for t in L.tracks_el if t.tag == "ReturnTrack"]
    for t in list(L.tracks_el):
        L.tracks_el.remove(t)
    for n in sorted(project.tracks):
        info = project.tracks[n]
        kind = info["type"]
        if kind not in PROTO:
            sys.exit(f"pista {n}: tipo desconocido '{kind}' (kit, drift, audio)")
        i = count[kind] = count.get(kind, -1) + 1
        tr = L.clone_track(kind, info["name"], COLORS[kind][i % len(COLORS[kind])])
        for h, db in zip(tr.findall("DeviceChain/Mixer/Sends/TrackSendHolder"), info["sends"]):
            h.find("Send/Manual").set("Value", str(db_to_gain(db)))
        if n in project.pads:
            if kind != "kit":
                sys.exit(f"pista {n}: 'pad' solo aplica a pistas kit")
            build_kit(L, tr, project.pads[n], samples)
        L.tracks_el.append(tr)
        built[n] = (tr, kind)
    for r in returns:
        L.tracks_el.append(r)

    # clips
    n_scenes = len(L.ls.findall("Scenes/Scene"))
    session = {}
    for ci, c in enumerate(project.clips):
        if c["track"] not in built:
            sys.exit(f"clip en pista {c['track']} que no existe")
        tr, kind = built[c["track"]]
        if not 1 <= c["scene"] <= n_scenes:
            sys.exit(f"escena {c['scene']} fuera de rango (1–{n_scenes})")
        value = slots(tr)[c["scene"] - 1].find("ClipSlot/Value")

        if isinstance(c["chain"], tuple) and c["chain"][0] == "rest":
            session[(c["track"], c["scene"])] = (None, c["chain"][1])   # silencio: ocupa tiempo, sin clip
            continue
        is_loop = isinstance(c["chain"], tuple)
        if kind == "audio":
            if not is_loop:
                sys.exit(f"pista {c['track']} es de audio: usa 'loop <archivo>'")
            ac = build_audio_clip(proto_audio, ci, c["chain"][1], samples)
            set_val(ac, "Color", tr.find("Color").get("Value"))
            value.append(ac)
            session[(c["track"], c["scene"])] = (ac, float(ac.find("CurrentEnd").get("Value")))
            continue
        if is_loop:
            sys.exit(f"pista {c['track']} es MIDI: necesita una cadena de phrases")

        notes, cmds, length = flatten(project, c["chain"], kind)
        clip = copy.deepcopy(proto_midi)
        clip.set("Id", str(ci))
        set_val(clip, "Color", tr.find("Color").get("Value"))
        clip.find("Name").set("Value", " ".join(f"{p}{'+' if t > 0 else ''}{t if t else ''}" for p, t in c["chain"]))
        for p in ("CurrentEnd", "Loop/LoopEnd", "Loop/OutMarker", "Loop/HiddenLoopEnd"):
            set_val(clip, p, f"{length:g}")
        envs = clip.find("Envelopes/Envelopes")
        for e in list(envs):
            envs.remove(e)
        write_notes(clip, notes)

        mixer = tr.find("DeviceChain/Mixer")
        sends = mixer.findall("Sends/TrackSendHolder")
        for letter in sorted({cm for _, cm, _ in cmds}):
            if letter in "AB":
                par = sends["AB".index(letter)].find("Send")
            elif letter == "V":
                par = mixer.find("Volume")
            elif letter == "P":
                par = mixer.find("Pan")
            elif letter in "FQ":
                af = L.ensure_autofilter(tr)
                par = af.find("Filter_Frequency" if letter == "F" else "Filter_Resonance")
            else:
                sys.exit(f"comando '{letter}' no soportado todavía (V P A B F Q)")
            changes = [(t, cmd_value(letter, v)) for t, cm, v in cmds if cm == letter]
            write_envelope(clip, target_id(par), float(par.find("Manual").get("Value")), changes)
        value.append(clip)
        session[(c["track"], c["scene"])] = (clip, length)

    for i, sc in enumerate(L.ls.findall("Scenes/Scene")):
        sc.find("Name").set("Value", f"ESC {i + 1:02d}")

    # Arrangement
    arr = Arrangement()
    if project.song:
        # lo que escribiste en 'song': escena x repeticiones, en ese orden
        t = 0.0
        for sc, rep in project.song:
            in_scene = {n: session[(n, sc)] for n in built if (n, sc) in session}
            if not in_scene:
                print(f"  aviso: la escena {sc} de la canción está vacía")
            span = max([ln for _, ln in in_scene.values()] + [PHRASE_LEN]) * rep
            for n, (clip, ln) in in_scene.items():
                if clip is not None:
                    arr.place(built[n][0], built[n][1], clip, t, span)
            t += span
        print(f"  arreglo: {len(project.song)} bloques, {t / 4:g} compases")
    else:
        # secciones como en LGPT: filas seguidas = sección; cada pista toca su columna
        n_sec, t = arrange_sections(arr, built, session, range(1, n_scenes + 1))
        print(f"  arreglo: {n_sec} secciones, {t / 4:g} compases")

    check(L.root)
    L.save(out)
    print(f"ok → {out}: {len(built)} pistas, {len(project.clips)} clips, {project.tempo:g} BPM")


def arrange_sections(arr, built, items, rows):
    """items[(pista, fila)] = (clip | None, largo). Filas seguidas con contenido forman una sección;
    una fila vacía la cierra. Dentro de la sección cada pista toca sus clips consecutivos en orden
    (None = silencio) y repite su bloque hasta que termina el más largo. La siguiente sección va
    inmediatamente después. Una pista vacía al inicio de la sección no suena en ella."""
    rows = list(rows)
    has = lambda r: any((k, r) in items for k in built)
    sections, i = [], 0
    while i < len(rows):
        if not has(rows[i]):
            i += 1
            continue
        a = i
        while i < len(rows) and has(rows[i]):
            i += 1
        sections.append(rows[a:i])
    t = 0.0
    for sec in sections:
        blocks = {}
        for k in built:
            col = []
            for r in sec:
                if (k, r) not in items:
                    break
                col.append(items[(k, r)])
            if col and sum(ln for _, ln in col) > 0:
                blocks[k] = col
        span = max([sum(ln for _, ln in col) for col in blocks.values()] + [PHRASE_LEN])
        for k, col in blocks.items():
            pos, j = 0.0, 0
            while pos < span:
                clip, ln = col[j % len(col)]
                if clip is not None and ln > 0:
                    arr.place(built[k][0], built[k][1], clip, t + pos, min(ln, span - pos))
                pos += max(ln, STEP)
                j += 1
        t += span
    return len(sections), t


class Arrangement:
    """Coloca copias de clips en la línea de tiempo (Arrangement View)."""

    def __init__(self):
        self.ids = {}

    def place(self, track, kind, clip, start, length):
        path = "Sample" if kind == "audio" else "ClipTimeable"
        events = track.find(f"DeviceChain/MainSequencer/{path}/ArrangerAutomation/Events")
        c = copy.deepcopy(clip)
        cid = self.ids[id(track)] = self.ids.get(id(track), 1000) + 1
        c.set("Id", str(cid))
        c.set("Time", f"{start:g}")
        set_val(c, "CurrentStart", f"{start:g}")
        set_val(c, "CurrentEnd", f"{start + length:g}")
        set_val(c, "Loop/LoopOn", "true")
        events.append(c)


def check(root):
    """Verificaciones que Live exige: Ids de parámetros únicos y no negativos, referencias válidas."""
    seen = {}
    for e in root.iter():
        if e.get("Id") is not None and (e.tag.endswith(POINTEE_TAGS) or e.tag.startswith("ControllerTargets")):
            if e.get("Id") in seen:
                sys.exit(f"Id duplicado {e.get('Id')} ({e.tag} / {seen[e.get('Id')]})")
            seen[e.get("Id")] = e.tag
        if (e.get("Id") or "0").startswith("-"):
            sys.exit(f"Id negativo en {e.tag}")
    for e in root.iter("PointeeId"):
        if e.get("Value") not in seen:
            sys.exit(f"envolvente apunta a un Id que no existe: {e.get('Value')}")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--real-dir=")]
    real = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--real-dir=")), None)
    if len(args) != 3:
        sys.exit(__doc__)
    export(parse(args[0]), args[1], args[2], real)
