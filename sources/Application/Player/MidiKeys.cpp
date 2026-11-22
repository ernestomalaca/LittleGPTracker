#include "MidiKeys.h"
#include "System/Console/Trace.h"

#if defined(__linux__)
#include <pthread.h>
#include <glob.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include <deque>
#include <stdlib.h>
#ifdef SDL2
#include <SDL2/SDL.h>
#endif

static pthread_mutex_t qLock = PTHREAD_MUTEX_INITIALIZER ;
static std::deque<MidiKeyEvent> queue_ ;
static bool told_ = false ;   // ya hay un aviso pendiente para la interfaz

static unsigned long nowMs() {
	struct timespec ts ;
	clock_gettime(CLOCK_MONOTONIC,&ts) ;
	return (unsigned long)ts.tv_sec*1000+ts.tv_nsec/1000000 ;
}

MidiKeys *MidiKeys::GetInstance() {
	static MidiKeys k ;
	return &k ;
}

MidiKeys::MidiKeys() : started_(false) {}

static void *midiThread(void *) {
	MidiKeys::GetInstance()->run() ;
	return 0 ;
}

void MidiKeys::Start() {
	if (started_) return ;
	started_ = true ;
	pthread_t t ;
	if (pthread_create(&t,0,midiThread,0)==0) pthread_detach(t) ;
}

void MidiKeys::push(unsigned char status, unsigned char d1, unsigned char d2) {
	MidiKeyEvent e ;
	e.status = status ;
	e.d1 = d1 ;
	e.d2 = d2 ;
	bool tell = false ;
	pthread_mutex_lock(&qLock) ;
	if (queue_.size() < 512) queue_.push_back(e) ;
	if (!told_) { told_ = true ; tell = true ; }
	pthread_mutex_unlock(&qLock) ;
#ifdef SDL2
	if (tell) {
		SDL_Event ev ;
		memset(&ev,0,sizeof(ev)) ;
		ev.type = SDL_USEREVENT ;
		ev.user.code = EVENT_CODE ;
		SDL_PushEvent(&ev) ;
	}
#endif
}

bool MidiKeys::Pop(MidiKeyEvent &e) {
	pthread_mutex_lock(&qLock) ;
	bool any = !queue_.empty() ;
	if (any) { e = queue_.front() ; queue_.pop_front() ; }
	else told_ = false ;   // vacía: el próximo mensaje vuelve a avisar
	pthread_mutex_unlock(&qLock) ;
	return any ;
}

struct MidiPort {
	std::string path ;
	int fd ;
	unsigned char run ;   // running status
	unsigned char buf[2] ;
	int need, have ;
	bool sysex ;
} ;

void MidiKeys::run() {
	std::vector<MidiPort> ports ;
	unsigned long lastScan = 0 ;
	for (;;) {
		unsigned long now = nowMs() ;
		if (lastScan == 0 || now - lastScan > 1000) {   // conectados nuevos
			lastScan = now ;
			glob_t g ;
			std::vector<std::string> found ;
			if (glob("/dev/snd/midiC*D*",0,0,&g) == 0)
				for (size_t i = 0; i < g.gl_pathc; i++) found.push_back(g.gl_pathv[i]) ;
			globfree(&g) ;
			// para probar sin controlador: TRK_MIDI_DEV=una fifo con bytes MIDI
			if (getenv("TRK_MIDI_DEV")) found.push_back(getenv("TRK_MIDI_DEV")) ;
			{
				for (size_t i = 0; i < found.size(); i++) {
					std::string p = found[i] ;
					bool open_ = false ;
					for (size_t k = 0; k < ports.size(); k++) if (ports[k].path == p) open_ = true ;
					if (open_) continue ;
					int fd = open(p.c_str(),O_RDONLY|O_NONBLOCK) ;
					if (fd < 0) continue ;
					MidiPort mp ;
					mp.path = p ;
					mp.fd = fd ;
					mp.run = 0 ;
					mp.need = mp.have = 0 ;
					mp.sysex = false ;
					ports.push_back(mp) ;
					Trace::Log("MIDI","controlador conectado: %s",p.c_str()) ;
				}
			}
		}
		if (ports.empty()) { usleep(250000) ; continue ; }
		std::vector<struct pollfd> pf(ports.size()) ;
		for (size_t k = 0; k < ports.size(); k++) {
			pf[k].fd = ports[k].fd ;
			pf[k].events = POLLIN ;
			pf[k].revents = 0 ;
		}
		int r = poll(&pf[0],pf.size(),200) ;
		if (r <= 0) continue ;
		for (size_t k = 0; k < ports.size();) {
			MidiPort &mp = ports[k] ;
			bool drop = (pf[k].revents & (POLLERR|POLLHUP|POLLNVAL)) != 0 ;
			if (!drop && (pf[k].revents & POLLIN)) {
				unsigned char b[256] ;
				int n = read(mp.fd,b,sizeof(b)) ;
				if (n < 0 && errno != EAGAIN && errno != EINTR) drop = true ;
				if (n == 0) drop = true ;
				for (int i = 0; i < n; i++) {
					unsigned char c = b[i] ;
					if (c >= 0xF8) continue ;                       // reloj y demás: nada
					if (c == 0xF0) { mp.sysex = true ; continue ; }
					if (c == 0xF7) { mp.sysex = false ; continue ; }
					if (c & 0x80) {
						mp.sysex = false ;
						if (c >= 0xF0) { mp.run = 0 ; continue ; }  // otros de sistema
						mp.run = c ;
						mp.have = 0 ;
						unsigned char hi = c & 0xF0 ;
						mp.need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2 ;
						continue ;
					}
					if (mp.sysex || !mp.run) continue ;
					mp.buf[mp.have++] = c ;
					if (mp.have < mp.need) continue ;
					mp.have = 0 ;
					unsigned char hi = mp.run & 0xF0 ;
					if (hi == 0x90 && mp.buf[1] == 0) push(0x80 | (mp.run & 0x0F),mp.buf[0],0) ;
					else if (hi == 0x80 || hi == 0x90 || hi == 0xB0) push(mp.run,mp.buf[0],mp.buf[1]) ;
				}
			}
			if (drop) {
				Trace::Log("MIDI","controlador desconectado: %s",mp.path.c_str()) ;
				close(mp.fd) ;
				ports.erase(ports.begin()+k) ;
				pf.erase(pf.begin()+k) ;
				continue ;
			}
			k++ ;
		}
	}
}

#else   // sin Linux: no hay controladores

MidiKeys *MidiKeys::GetInstance() { static MidiKeys k ; return &k ; }
MidiKeys::MidiKeys() : started_(false) {}
void MidiKeys::Start() {}
bool MidiKeys::Pop(MidiKeyEvent &) { return false ; }
void MidiKeys::run() {}
void MidiKeys::push(unsigned char,unsigned char,unsigned char) {}

#endif
