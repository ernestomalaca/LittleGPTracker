#ifndef _AUDIO_WATCH_H_
#define _AUDIO_WATCH_H_
// trk2als: para el log: qué parte del audio se detuvo mientras se cargaba
// un sonido. Marcas en ms (SDL_GetTicks) desde los hilos del audio; el hilo
// de la interfaz pide el resumen.
//   salida  = la salida de audio (SDL/ALSA) pidiendo bloques
//   mezcla  = el mezclador terminando bloques
//   candado = el mezclador esperando el candado
void AudioWatchOut() ;          // cada vez que la salida pide un bloque
void AudioWatchWaitBegin() ;    // el mezclador va a pedir el candado
void AudioWatchWaitEnd() ;      // ya lo tiene
void AudioWatchMixDone() ;      // terminó el bloque
void AudioWatchReset() ;
// lo más que pasó sin que la salida pidiera / sin que se mezclara / esperando el candado (ms)
void AudioWatchTake(unsigned &outGap, unsigned &mixGap, unsigned &lockWait) ;
#endif
