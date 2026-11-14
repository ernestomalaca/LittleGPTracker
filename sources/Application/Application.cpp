#include "Application/Application.h" 
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

static void installCrashLog() {
	if (getenv("TRK_NOCRASHLOG")) return ;   // p. ej. al depurar con AddressSanitizer
	Path p("root:crash.log") ;
	strncpy(crashPath_,p.GetPath().c_str(),sizeof(crashPath_)-1) ;
	int sigs[5]={SIGSEGV,SIGBUS,SIGFPE,SIGABRT,SIGILL} ;
	for (int i=0;i<5;i++) signal(sigs[i],crashHandler) ;
}
#else
static void installCrashLog() {}
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
	return true ;
} ;

GUIWindow *Application::GetWindow() {
	return window_ ;
} ;

Application::~Application() {
	delete window_ ;
}
