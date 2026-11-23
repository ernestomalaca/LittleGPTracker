from reportlab.lib.pagesizes import A5
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table,
                                TableStyle, Image, PageBreak, KeepTogether)
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib import colors
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
import sys

D = '/usr/share/fonts/truetype/dejavu/'
pdfmetrics.registerFont(TTFont('S', D + 'DejaVuSans.ttf'))
pdfmetrics.registerFont(TTFont('SB', D + 'DejaVuSans-Bold.ttf'))
pdfmetrics.registerFont(TTFont('M', D + 'DejaVuSansMono-Bold.ttf'))
from reportlab.pdfbase.pdfmetrics import registerFontFamily
registerFontFamily('S', normal='S', bold='SB', italic='S', boldItalic='SB')

INK = colors.HexColor('#1d1d25'); ACC = colors.HexColor('#5b6fe0'); SOFT = colors.HexColor('#eef0fb')
WARN = colors.HexColor('#c0392b'); DIM = colors.HexColor('#6a6880')
body = ParagraphStyle('b', fontName='S', fontSize=8.6, leading=12.2, textColor=INK, spaceAfter=4)
h1 = ParagraphStyle('h1', fontName='SB', fontSize=15, leading=19, textColor=INK, spaceBefore=4, spaceAfter=6)
h2 = ParagraphStyle('h2', fontName='SB', fontSize=10.5, leading=14, textColor=ACC, spaceBefore=8, spaceAfter=3)
small = ParagraphStyle('s', parent=body, fontSize=7.4, leading=10, textColor=DIM)
key = ParagraphStyle('k', fontName='M', fontSize=7.8, leading=10.5, textColor=INK)
cell = ParagraphStyle('c', parent=body, fontSize=8, leading=10.8, spaceAfter=0)

W, H = A5
M = 14 * mm


def deco(c, doc):
    c.saveState()
    c.setFillColor(WARN); c.setFont('SB', 7)
    c.drawString(M, H - 9 * mm, 'ALPHA · RG35XX PLUS · SIN PROBAR EN ESTA CONSOLA')
    c.setFillColor(DIM); c.setFont('S', 7)
    c.drawRightString(W - M, 8 * mm, 'Chancho 6 · manual alpha · %d' % doc.page)
    c.restoreState()


def cover(c, doc):
    c.saveState()
    c.setFillColor(colors.HexColor('#101018')); c.rect(0, 0, W, H, fill=1, stroke=0)
    c.setFillColor(colors.HexColor('#8c9eff')); c.setFont('M', 34)
    c.drawString(M, H - 60 * mm, 'CHANCHO 6')
    c.setFillColor(colors.HexColor('#f0eefa')); c.setFont('S', 12)
    c.drawString(M, H - 70 * mm, 'tracker de bolsillo, con Session como Live')
    c.drawString(M, H - 77 * mm, 'para la Anbernic RG35XX Plus')
    c.setFillColor(WARN); c.roundRect(M, H - 100 * mm, 34 * mm, 12 * mm, 3 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white); c.setFont('SB', 16); c.drawCentredString(M + 17 * mm, H - 96 * mm, 'ALPHA')
    c.setFillColor(colors.HexColor('#9a98b0')); c.setFont('S', 8)
    t = c.beginText(M, H - 112 * mm); t.setLeading(11)
    for l in ['Versión de prueba. Compilada para la RG35XX Plus pero nunca',
              'probada en esa consola: puede no arrancar, los botones pueden',
              'estar cruzados o el audio puede fallar.',
              'Haz copia de tus proyectos y cuéntanos qué pasó.']:
        t.textLine(l)
    c.drawText(t)
    c.setFont('S', 7); c.drawString(M, 12 * mm, 'Basado en LittleGPTracker (piggy). Manual de la versión alpha, noviembre 2026.')
    c.restoreState()


def P(t, s=body): return Paragraph(t, s)


def keys(rows, w=(32 * mm, None)):
    data = [[P(a, key), P(b, cell)] for a, b in rows]
    tw = W - 2 * M
    t = Table(data, colWidths=[w[0], tw - w[0]])
    t.setStyle(TableStyle([
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('ROWBACKGROUNDS', (0, 0), (-1, -1), [SOFT, colors.white]),
        ('LEFTPADDING', (0, 0), (-1, -1), 4), ('RIGHTPADDING', (0, 0), (-1, -1), 4),
        ('TOPPADDING', (0, 0), (-1, -1), 3), ('BOTTOMPADDING', (0, 0), (-1, -1), 3),
    ]))
    return t


def box(t, bg=colors.HexColor('#fdecea'), fg=WARN):
    tb = Table([[P(t, ParagraphStyle('w', parent=body, textColor=fg, spaceAfter=0))]], colWidths=[W - 2 * M])
    tb.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), bg), ('BOX', (0, 0), (-1, -1), 0.8, fg),
                            ('LEFTPADDING', (0, 0), (-1, -1), 7), ('RIGHTPADDING', (0, 0), (-1, -1), 7),
                            ('TOPPADDING', (0, 0), (-1, -1), 6), ('BOTTOMPADDING', (0, 0), (-1, -1), 6)]))
    return tb


S = []
S.append(PageBreak())

S += [P('Antes de empezar', h1),
      box('<b>Esto es una alpha.</b> Chancho 6 se hace y se prueba en la RG353V. Esta versión para la '
          'RG35XX Plus está compilada para su procesador, pero <b>nadie la ha probado todavía en una RG35XX '
          'Plus</b>. Lo más probable es que haya que ajustar los botones. Si algo falla, manda '
          '<font name="M">chancho6/log.txt</font> y <font name="M">chancho6/botones.txt</font>.'),
      Spacer(1, 6),
      P('Chancho 6 es un tracker (como LittleGPTracker o LSDJ) con una <b>Session</b> parecida a la de '
        'Ableton Live: una rejilla de clips por pista y escenas que se lanzan en el siguiente compás. '
        'Toca clips de audio estirados a tempo, samples, kits e instrumentos multicapa.'),
      P('Qué necesitas', h2),
      P('Una RG35XX Plus con un sistema que tenga <b>Ports</b>: muOS o Knulli (recomendados). En el sistema '
        'de fábrica de Anbernic se puede intentar desde APPS. Una tarjeta con ~100 MB libres.'),
      P('Instalar', h2),
      keys([('1', 'Descomprime el zip. Copia a la carpeta de Ports de la tarjeta (Knulli: <font name="M">roms/ports</font>; '
                   'muOS: <font name="M">ROMS/ports</font> o la carpeta de aplicaciones según la versión): '
                   '<font name="M">Chancho 6.sh</font>, <font name="M">Detectar botones.sh</font> y la carpeta '
                   '<font name="M">chancho6/</font>.'),
            ('2', '(Opcional) tu librería de samples en <font name="M">chancho6/samplelib</font>.'),
            ('3', 'Reinicia la lista de juegos si hace falta y abre <b>Ports › Chancho 6</b>.')], (8 * mm, None)),
      P('Si los botones no responden bien', h2),
      P('Abre <b>Ports › Detectar botones</b> y presiona en este orden: arriba, abajo, izquierda, derecha, '
        'A, B, X, Y, L, R, Select, Start. Sal con Project › Exit. Lo que presionaste queda en '
        '<font name="M">chancho6/botones.txt</font>: mándalo y con eso se corrige '
        '<font name="M">chancho6/mapping.xml</font>.'),
      P('Lo que no trae esta versión', h2),
      P('• Teclado MIDI por USB (en la RG353V sí).<br/>• Actualizar por Wi-Fi: se actualiza copiando el '
        '<font name="M">.elf</font> nuevo dentro de <font name="M">chancho6/</font>.'),
      PageBreak()]

S += [P('El set demo', h1),
      P('El zip trae <font name="M">chancho6/lgpt_chancho_try</font>: un set hecho en Ableton Live y pasado a '
        'Chancho con <i>live2chancho</i>. Son 5 pistas de audio a 93 bpm (melodía, acordes, acordeón, bajo y '
        'un kit sudamericano) repartidas en escenas.'),
      P('1. Al abrir Chancho sale la lista de proyectos: elige <b>chancho_try</b> con A.<br/>'
        '2. Estás en la Session (SES arriba). Muévete con las flechas.<br/>'
        '3. Ve a la columna de escenas (← desde la primera pista) y presiona A: la escena entra.<br/>'
        '4. Prueba lanzar clips sueltos con A, y Select + Start para detener todo en el compás.'),
      Spacer(1, 4),
      Image('/tmp/claude-0/-home-claude/8b7e166f-05e8-513f-95ea-580c3fb2a69c/scratchpad/at/try2.png',
            width=W - 2 * M, height=(W - 2 * M) * 0.75),
      P('La Session con el demo: escenas a la izquierda (03 suena, ▶ verde), clips de audio por pista, '
        'y la cabecera de cada pista arriba.', small),
      PageBreak()]

S += [P('Controles básicos', h1),
      P('Pantallas', h2),
      P('Arriba se ve dónde estás: <b>SES</b> (Session), <b>CLP</b> (clip), <b>INS</b> (instrumento), '
        '<b>MIX</b> (mezcla) y el proyecto. Select + flechas cambia de pantalla.'),
      keys([('Flechas', 'mover el cursor'),
            ('A', 'poner, lanzar, confirmar'),
            ('A + flechas', 'cambiar un valor (← → poco, ↑ ↓ mucho)'),
            ('B + A', 'borrar'),
            ('Select + flechas', 'cambiar de pantalla'),
            ('Start', 'transporte: arranca o detiene todo'),
            ('Select + L / R', 'deshacer / rehacer'),
            ('L2 + R2', 'ayuda'),
            ('R2', 'captura (lo último que tocaste o moviste)')]),
      P('Session', h2),
      keys([('A en un clip', 'lo lanza en el siguiente compás (todo parado: ya). Otra vez A: desde el inicio'),
            ('A en celda vacía', 'detiene esa pista en el siguiente compás (la celda parpadea en blanco)'),
            ('Select + Start', 'Stop All Clips: todo se detiene al terminar el compás; el reloj sigue'),
            ('B + Select', 'mute de la pista (soltando B primero, se queda)'),
            ('B + Start', 'solo de la pista (igual)'),
            ('L / R', 'una página abajo / arriba (como la octava en el clip)'),
            ('R ×3 rápido', 'a la cabecera de la pista (cambiar su sonido)'),
            ('Select + →', 'entra al clip para editarlo'),
            ('Select + B', 'marca clips de una fila; ← → agranda; A los lanza juntos')]),
      PageBreak()]

S += [P('Escenas', h1),
      P('La columna de la izquierda son las escenas (← desde la primera pista, → vuelve). A lanza la '
        'escena: entran los clips de esa fila y las pistas sin clip ahí se detienen.'),
      keys([('Y + ↑ / ↓', 'repeticiones: ∞ (se queda) o x1 … x16. Una vuelta = el clip más largo de la '
                           'escena. Al terminar entra la escena de abajo; si está vacía, todo se detiene'),
            ('Select, B', 'marca escenas (↑ ↓ agranda); B copia'),
            ('Select + A', 'con marca: duplica justo abajo. Sin marca: pega lo copiado'),
            ('B + A', 'borra la escena (lo de abajo sube)'),
            ('Select + L', 'deshace')]),
      P('Lo que espera el 1 parpadea: clips lanzados, celdas que detienen su pista y la escena lanzada. Lo '
        'que suena se queda fijo hasta que se detiene.'),
      P('Clips de audio', h2),
      P('Cada celda de audio es su propio clip (como en Live): copiar, pegar o duplicar crea uno nuevo, '
        'con su región, warp y ganancia; el archivo se carga una sola vez. Hasta 191 por proyecto '
        '(A00–ABE). En INS se edita: desde, loop, largo, warp (complex o re-pitch), transposición, '
        'ganancia y fade.'),
      P('La primera vez que suena un loop en <i>complex</i> a un tempo nuevo, Chancho lo estira por detrás '
        'y entra en el siguiente 1 ya listo (puede tardar un compás o dos). Lo estirado se guarda en '
        '<font name="M">chancho6/warpcache</font>.'),
      P('Sonidos', h2),
      P('Desde la cabecera de una pista (R ×3), A sobre un sonido del navegador lo lanza a la pista en el '
        'siguiente compás. X prende o apaga la pre-escucha. Los presets viven en '
        '<font name="M">chancho6/presets</font> (Kits, Multicapa, Samples) y tu librería en '
        '<font name="M">chancho6/samplelib</font>. Esta alpha no trae presets: pon los tuyos ahí.'),
      PageBreak()]

S += [P('Más', h1),
      P('chancho.local (archivos desde la compu o el celular)', h2),
      P('Préndelo con el botón WEB en la pantalla de Proyecto. Con la consola en el mismo Wi-Fi abre '
        '<font name="M">http://chancho.local:8080</font> (o la IP que sale en Proyecto). Puedes bajar un '
        'proyecto en zip, subir samples arrastrándolos y exportar el proyecto a Ableton Live.'),
      P('Ableton Link', h2),
      P('En Proyecto, ABLETON LINK: <i>si</i> sigue el tempo de Live o cualquier app con Link; '
        '<i>si + play</i> además arranca y para con los demás. Necesita Wi-Fi.'),
      P('RECOPILAR', h2),
      P('Los samples se usan desde donde están. Antes de compartir una canción, Proyecto › RECOPILAR '
        'copia todo a la carpeta del proyecto.'),
      P('Ajustes (chancho6/config.xml)', h2),
      keys([('AUDIOBUFFERSIZE', 'si el audio se corta, súbelo (1024)'),
            ('MAXTRACKS', 'pistas máximas (6)'),
            ('WEBSERVER', 'YES: chancho.local prendido desde el inicio'),
            ('LOOPWARP', 'complex: los loops nuevos entran en complex')], (34 * mm, None)),
      P('Si algo falla — cómo reportar', h2),
      box('Manda estas cosas a quien te pasó Chancho:<br/>'
          '1. <font name="M">chancho6/log.txt</font> (y <font name="M">log-1.txt</font> si volviste a abrir)<br/>'
          '2. <font name="M">chancho6/botones.txt</font> después de correr Detectar botones<br/>'
          '3. Qué sistema usas (muOS, Knulli, fábrica) y su versión<br/>'
          '4. Qué hiciste justo antes del problema',
          bg=SOFT, fg=ACC)]

doc = BaseDocTemplate(sys.argv[1], pagesize=A5, leftMargin=M, rightMargin=M, topMargin=14 * mm,
                      bottomMargin=14 * mm, title='Chancho 6 — manual ALPHA (RG35XX Plus)',
                      author='Chancho 6')
fr = Frame(M, 14 * mm, W - 2 * M, H - 28 * mm, id='f')
doc.addPageTemplates([PageTemplate('cover', [fr], onPage=cover), PageTemplate('p', [fr], onPage=deco)])
from reportlab.platypus import NextPageTemplate
doc.build([NextPageTemplate('p')] + S)
