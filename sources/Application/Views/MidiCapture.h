#ifndef _MIDI_CAPTURE_H_
#define _MIDI_CAPTURE_H_

// trk2als: capturar lo que tocaste (como Capture MIDI de Live). Lo que entra
// por el teclado MIDI se anota siempre (los últimos ~60 s); R2 lo vuelve clip:
//  - sin nada sonando: un clip nuevo, el primer golpe en el 1.1.1; adivina el
//    tempo (y se lo pone al proyecto) y el largo (1, 2, 4 u 8 compases);
//    solo la última idea: si tocaste la frase varias veces, la última vuelta
//    entera; si no, los últimos 8 compases. Lo de antes se olvida
//  - sonando, con un clip MIDI en la pista: encima de ese clip, lo que tocaste
//    en la última vuelta del loop
//  - sonando, sin clip en la pista: un clip nuevo en la rejilla de la canción

#include <vector>
#include "Application/Model/Clip.h"

class ViewData ;

class MidiCapture {
public:
	// cada nota que entra (vel 0 = se soltó); ch = la pista que la tocó
	static void Note(int ch,int pitch,int vel) ;
	// cuándo llegó la última nota aún sin capturar (0 = nada)
	static unsigned long LastNoteMs() ;
	// lo vuelve clip; devuelve el aviso (o 0 si no había nada). Si cambió un
	// clip que ya existía, 'beforeClip' >= 0 y 'before' es como estaba (deshacer)
	// 'playRow' >= 0: se hizo un clip nuevo: su fila (para que arranque a
	// sonar en seguida, como en Live)
	// 'closed': sin nada sonando, capturaste justo al terminar la vuelta (sigue en
	// loop en seguida); si no, el clip entra en el 1 tras un compás de metrónomo
	static const char *Capture(ViewData *d,int &beforeClip,Clip &before,int &playRow,bool &closed) ;
	// la silueta: lo que caería encima de ese clip si capturas ahora
	// (en el clip vacío que se está viendo: lo que se crearía; 'pages' = sus compases)
	static void Preview(ViewData *d,int clip,std::vector<ClipNote> &out,int *pages=0) ;
	// olvidar lo tocado (para intentar de nuevo); true si había algo
	static bool Forget(int ch) ;
	// las teclas que están apretadas ahora en esa pista
	static void Held(int ch,std::vector<int> &pitches) ;
	// el pianito de la barra de arriba: por cada nota de la octava (C..B), la
	// pista que la toca ahora (la última), o -1
	static void HeldClasses(int out[12]) ;
} ;

#endif
