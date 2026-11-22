#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "Foundation/T_Singleton.h"
#include "Foundation/Variables/VariableContainer.h"
#include "System/Console/Trace.h"
#include "Externals/TinyXML/tinyxml.h"

class Config: public T_Singleton<Config>,public VariableContainer {
public:
	Config() ;
	~Config() ;
	const char *GetValue(const char *key) ;
	void ProcessArguments(int argc,char **argv) ;
	// trk2als: cambia (o agrega) un valor y lo guarda en config.xml
	bool SetPersistent(const char *key,const char *value) ;
} ;

#endif
