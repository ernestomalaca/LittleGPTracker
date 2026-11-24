#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "Foundation/T_Singleton.h"
#include "Foundation/Variables/VariableContainer.h"
#include "System/Console/Trace.h"
#include "Externals/TinyXML/tinyxml.h"
#include <map>
#include <string>

class Config: public T_Singleton<Config>,public VariableContainer {
public:
	Config() ;
	~Config() ;
	const char *GetValue(const char *key) ;
	void ProcessArguments(int argc,char **argv) ;
	// trk2als: cambia (o agrega) un valor y lo guarda en config.xml
	bool SetPersistent(const char *key,const char *value) ;
	// trk2als: temas (chancho6/temas/<nombre>.xml). "lavanda" = los colores de
	// config.xml. Aplica el tema a los valores en memoria; Generation() sube con
	// cada cambio, para que quien guarde colores en caché los vuelva a leer.
	bool ApplyTheme(const char *name) ;
	static int Generation() { return generation_ ; }
	void Set(const char *key,const char *value) ;
private:
	static int generation_ ;
	std::map<std::string,std::string> themeBase_ ;
	bool themeBaseTaken_ ;
} ;

#endif
