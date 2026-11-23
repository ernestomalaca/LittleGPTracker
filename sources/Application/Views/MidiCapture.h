#ifndef _MIDI_CAPTURE_H_
#define _MIDI_CAPTURE_H_

// trk2als: capturar lo que tocaste (como Capture MIDI de Live). Lo que entra
// por el teclado MIDI se anota siempre (los últimos ~60 s); R2 lo vuelve clip:
//  - sin nada sonando: un clip nuevo, el primer golpe en el 1.1.1; adivina el
//    tempo (y se lo pone al proyecto) y el largo (1, 2, 4, 8 o 16 compases);
//    si tocaste la frase varias veces, el clip la guarda toda y el loop queda
//    en la última vuelta
//  - sonando, con un clip MIDI en la pista: encima de ese clip, lo que tocaste
//    en la última vuelta del loop
//  - sonando, sin clip en la pista: un clip nuevo en la rejilla de la canción

class ViewData ;
class Clip ;

class MidiCapture {
public:
	// cada nota que entra (vel 0 = se soltó); ch = la pista que la tocó
	static void Note(int ch,int pitch,int vel) ;
	// cuándo llegó la última nota aún sin capturar (0 = nada)
	static unsigned long LastNoteMs() ;
	// lo vuelve clip; devuelve el aviso (o 0 si no había nada). Si cambió un
	// clip que ya existía, 'beforeClip' >= 0 y 'before' es como estaba (deshacer)
	static const char *Capture(ViewData *d,int &beforeClip,Clip &before) ;
} ;

#endif
