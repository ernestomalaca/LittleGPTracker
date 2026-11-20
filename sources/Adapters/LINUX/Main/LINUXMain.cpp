#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "System/Console/Trace.h"
#include "Adapters/LINUX/System/LINUXSystem.h"
#include "Adapters/SDL2/GUI/SDLGUIWindowImp.h"
#include "Application/Application.h"
/*
 * generic entrypoint for linux based targets
 */
int main(int argc,char *argv[]) {
	LINUXSystem::Boot(argc,argv);

	SDLCreateWindowParams params;
	params.title="littlegptracker";
	params.cacheFonts_=true;

	Application::GetInstance()->Init(params);

	int rc = LINUXSystem::MainLoop();
	// trk2als: salir ya, sin desarmar nada: con la música andando y Link
	// prendido, desarmar los hilos del audio y de Link al final podía dejar
	// la consola en negro. El sistema cierra el audio y los archivos solo
	Trace::Log("EVENT", "saliendo (rc %d)", rc);
	fflush(stdout);
	fflush(stderr);
	_exit(rc);
}

void _assert() {};
