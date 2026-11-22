#ifndef _MIDI_KEYS_H_
#define _MIDI_KEYS_H_

// trk2als: un controlador MIDI por USB (teclado, pads, perillas). Un hilo
// lee los puertos crudos de ALSA (/dev/snd/midiC*D*), sin librerías: los
// abre al conectarlos y los suelta al desconectarlos. Los mensajes van a una
// cola y se le avisa al hilo de la interfaz (como la repetición de botones):
// ahí se tocan, con el mismo cuidado que un botón.

struct MidiKeyEvent {
	unsigned char status, d1, d2 ;   // nota: 0x90 / 0x80 (vel 0 = 0x80); CC: 0xB0
} ;

class MidiKeys {
public:
	static MidiKeys *GetInstance() ;
	void Start() ;                    // arranca el hilo (una vez)
	bool Pop(MidiKeyEvent &e) ;       // lo que llegó, en orden
	enum { EVENT_CODE = 0x4D49 } ;    // SDL_USEREVENT: hay MIDI en la cola
	// para el hilo
	void run() ;
private:
	MidiKeys() ;
	void push(unsigned char status, unsigned char d1, unsigned char d2) ;
	bool started_ ;
} ;

#endif
