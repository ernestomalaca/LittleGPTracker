#include "Application/Application.h" 
#ifdef WEBSERVER
#include "Services/Web/WebServer.h"
#endif
#include "Application/AppWindow.h" 
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"
#include "Application/Persistency/PersistencyService.h" 
#include "Services/Audio/Audio.h"
#include "Application/Commands/CommandDispatcher.h"
#include "Application/Controllers/ControlRoom.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"

#include <math.h>

// trk2als: si el programa truena, deja crash.log (en la carpeta raíz) con la
// pila de llamadas: dónde falló, para poder arreglarlo
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include "Application/Mixer/MixerService.h"
#include <stdlib.h>

static char crashPath_[1024] ;

static void crashWrite(int fd,const char *s) {
	if (write(fd,s,strlen(s))<0) {}
}

static void crashHandler(int sig) {
	int fd=open(crashPath_,O_WRONLY|O_CREAT|O_APPEND,0644) ;
	if (fd>=0) {
		char head[160] ;
		time_t now=time(0) ;
		struct tm t ;
		localtime_r(&now,&t) ;
		strftime(head,sizeof(head),"\n=== crash %Y-%m-%d %H:%M:%S",&t) ;
		crashWrite(fd,head) ;
		crashWrite(fd,sig==SIGSEGV?" SIGSEGV (memoria)\n":sig==SIGBUS?" SIGBUS (memoria)\n":
		              sig==SIGFPE?" SIGFPE (division)\n":sig==SIGABRT?" SIGABRT (abort)\n":
		              sig==SIGILL?" SIGILL\n":"\n") ;
		void *frames[64] ;
		int n=backtrace(frames,64) ;
		backtrace_symbols_fd(frames,n,fd) ;
		close(fd) ;
	}
	signal(sig,SIG_DFL) ;   // que el sistema también lo registre
	raise(sig) ;
}

// trk2als: si la interfaz se queda pensando más de 5 s en algo (congelada),
// se anota en crash.log dónde está cada hilo: la interfaz y quien tenga el
// candado del mezclador
static pthread_t mainThread_ ;
static volatile int busy_ = 0 ;
static volatile unsigned long busyGen_ = 0 ;

static void stackHandler(int) {
	int fd=open(crashPath_,O_WRONLY|O_CREAT|O_APPEND,0644) ;
	if (fd<0) return ;
	char head[80] ;
	snprintf(head,sizeof(head),"--- hilo %lu%s\n",(unsigned long)pthread_self(),
	         pthread_equal(pthread_self(),mainThread_)?" (la interfaz)":"") ;
	crashWrite(fd,head) ;
	void *frames[64] ;
	int n=backtrace(frames,64) ;
	backtrace_symbols_fd(frames,n,fd) ;
	close(fd) ;
}

static void *watchdog(void *) {
	unsigned long gen=0 ;
	int still=0 ;
	bool told=false ;
	for (;;) {
		sleep(1) ;
		if (!busy_ || busyGen_!=gen) { gen=busyGen_ ; still=0 ; told=false ; continue ; }
		if (++still<5 || told) continue ;
		told=true ;
		int fd=open(crashPath_,O_WRONLY|O_CREAT|O_APPEND,0644) ;
		int depth=0 ;
		unsigned long owner=0 ;
		MixerService::GetInstance()->Debug(depth,owner) ;
		if (fd>=0) {
			char head[200] ;
			time_t now=time(0) ;
			struct tm t ;
			localtime_r(&now,&t) ;
			strftime(head,sizeof(head),"\n=== congelado %Y-%m-%d %H:%M:%S (5 s sin responder)",&t) ;
			crashWrite(fd,head) ;
			snprintf(head,sizeof(head),"; candado del mezclador: %d, hilo %lu\n",depth,owner) ;
			crashWrite(fd,head) ;
			close(fd) ;
		}
		pthread_kill(mainThread_,SIGUSR2) ;
		usleep(300000) ;
		if (depth>0 && owner && !pthread_equal((pthread_t)owner,mainThread_)) {
			pthread_kill((pthread_t)owner,SIGUSR2) ;
			usleep(300000) ;
		}
	}
	return 0 ;
}

void AppBusy(bool on) {
	if (on) busyGen_++ ;
	busy_=on ;
}

static void installCrashLog() {
	if (getenv("TRK_NOCRASHLOG")) return ;   // p. ej. al depurar con AddressSanitizer
	Path p("root:crash.log") ;
	strncpy(crashPath_,p.GetPath().c_str(),sizeof(crashPath_)-1) ;
	// el registro corre en una pila aparte: si el crash fue por pisar la
	// pila (un búfer desbordado), la normal ya no sirve para escribir nada
	static char altStack[64*1024] ;
	stack_t ss ;
	ss.ss_sp=altStack ;
	ss.ss_size=sizeof(altStack) ;
	ss.ss_flags=0 ;
	sigaltstack(&ss,0) ;
	int sigs[5]={SIGSEGV,SIGBUS,SIGFPE,SIGABRT,SIGILL} ;
	for (int i=0;i<5;i++) {
		struct sigaction sa ;
		memset(&sa,0,sizeof(sa)) ;
		sa.sa_handler=crashHandler ;
		sa.sa_flags=SA_ONSTACK ;
		sigemptyset(&sa.sa_mask) ;
		sigaction(sigs[i],&sa,0) ;
	}
	struct sigaction su ;
	memset(&su,0,sizeof(su)) ;
	su.sa_handler=stackHandler ;
	su.sa_flags=SA_ONSTACK|SA_RESTART ;
	sigemptyset(&su.sa_mask) ;
	sigaction(SIGUSR2,&su,0) ;
	{ void *f[4] ; backtrace(f,4) ; }   // carga lo que necesita ahora, no dentro del aviso
	mainThread_=pthread_self() ;
	pthread_t w ;
	if (pthread_create(&w,0,watchdog,0)==0) pthread_detach(w) ;
}
#else
static void installCrashLog() {}
void AppBusy(bool) {}
#endif

Application *Application::instance_=NULL ;

Application::Application() {
}

void Application::initMidiInput()
{
  const char *preferedDevice=Config::GetInstance()->GetValue("MIDICTRLDEVICE");

  IteratorPtr<MidiInDevice>it(MidiService::GetInstance()->GetInIterator()) ;
  for(it->Begin();!it->IsDone();it->Next())
  {
    MidiInDevice &in=it->CurrentItem() ;
    if ((preferedDevice) && (!strncmp(in.GetName(), preferedDevice, strlen(preferedDevice))))
    {
      if (in.Init())
      {
        if (in.Start())
        {
          Trace::Log("MIDI","Controlling activated for MIDI interface %s",in.GetName()) ;
        }
        else
        {
          in.Close() ;
        }
      }
    }
  }
}

bool Application::Init(GUICreateWindowParams &params) {
	const char* root=Config::GetInstance()->GetValue("ROOTFOLDER") ;
	if (root) {
		Path::SetAlias("root",root) ;
	} ;
	installCrashLog() ;
	window_=AppWindow::Create(params) ;
	PersistencyService::GetInstance() ;
  Audio *audio=Audio::GetInstance() ;
  audio->Init() ;
	CommandDispatcher::GetInstance()->Init() ;
  initMidiInput();
	ControlRoom::GetInstance()->LoadMapping("bin:mapping.xml") ;
#ifdef WEBSERVER
	// trk2als: chancho.local va apagado; se prende con el botón WEB del
	// Proyecto (o desde el arranque con WEBSERVER YES en config.xml)
	if (WebServer::GetInstance()->AutoStart()) WebServer::GetInstance()->Start() ;
#endif
	return true ;
} ;

GUIWindow *Application::GetWindow() {
	return window_ ;
} ;

Application::~Application() {
	delete window_ ;
}
