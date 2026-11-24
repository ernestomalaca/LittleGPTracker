#include "Config.h"
#include "System/FileSystem/FileSystem.h"

int Config::generation_ = 0 ;

// lo que un tema puede cambiar
static const char *themeKeys[] = {
	"BACKGROUND", "FOREGROUND", "BORDER", "HICOLOR1", "HICOLOR2", "SONGVIEW_FE",
	"SONGVIEW_00", "CURSORCOLOR", "PLAYCOLOR", "MUTECOLOR", "ROWCOLOR1", "ROWCOLOR2",
	"MAJORBEAT", "COL_TITLE", "TRACKCOLORS", "GREENCOLOR", "ICONCOLORS", "DITHER", 0} ;


Config::Config() 
{
	themeBaseTaken_ = false ;
	Path path("bin:config.xml") ;
	Trace::Log("CONFIG","Got config path=%s",path.GetPath().c_str()) ;
	TiXmlDocument *document=new TiXmlDocument(path.GetPath());
	bool loadOkay = document->LoadFile();

	if (loadOkay) 
  { 
		// Check first node is CONFIG/ GPCONFIG

		TiXmlNode* rootnode = 0;

		rootnode = document->FirstChild( "CONFIG" );
		if (!rootnode)
    {
		   rootnode = document->FirstChild( "GPCONFIG" );
    }
    
		if (rootnode)
    {
			TiXmlElement *rootelement = rootnode->ToElement();
			TiXmlNode *node = rootelement->FirstChildElement() ;

			// Loop on all children
		
			if (node)
      {
				TiXmlElement *element = node->ToElement();
				while (element) 
        {
					const char *key=element->Value() ;
					const char *value=element->Attribute("value") ;
					if (!value)
          {
						value=element->Attribute("VALUE") ;
					}
					if (key&&value)
          {
						Variable *v=new Variable(key,0,value) ;
						Insert(v) ;
					}
					element = element->NextSiblingElement(); 
				}
			}

    }
    } else {
		Trace::Log("CONFIG","No (bad?) config.xml") ;
	}
 	delete(document) ;
	// el tema elegido en la consola (Proyecto > TEMA)
	Variable *t = FindVariable("THEME") ;
	if (t && t->GetString() && *t->GetString()) ApplyTheme(t->GetString()) ;
}

void Config::Set(const char *key,const char *value)
{
	Variable *v=FindVariable(key) ;
	if (v) v->SetString(value) ;
	else Insert(new Variable(key,0,value)) ;
}

bool Config::ApplyTheme(const char *name)
{
	if (!themeBaseTaken_) {   // los valores de config.xml: la base (lavanda)
		themeBaseTaken_ = true ;
		for (int i = 0; themeKeys[i]; i++) {
			Variable *v = FindVariable(themeKeys[i]) ;
			themeBase_[themeKeys[i]] = (v && v->GetString()) ? v->GetString() : "" ;
		}
	}
	std::map<std::string,std::string> vals = themeBase_ ;
	bool ok = true ;
	if (name && *name && strcmp(name, "lavanda")) {
		std::string fn = std::string("bin:temas/") + name + ".xml" ;
		Path path(fn.c_str()) ;
		TiXmlDocument doc(path.GetPath()) ;
		if (doc.LoadFile()) {
			TiXmlNode *root = doc.FirstChild("TEMA") ;
			if (!root) root = doc.FirstChild("CONFIG") ;
			TiXmlElement *e = root ? root->FirstChildElement() : doc.FirstChildElement() ;
			for (; e; e = e->NextSiblingElement()) {
				const char *val = e->Attribute("value") ;
				if (!val) val = e->Attribute("VALUE") ;
				if (!val) continue ;
				for (int i = 0; themeKeys[i]; i++)
					if (!strcmp(themeKeys[i], e->Value())) vals[themeKeys[i]] = val ;
			}
		} else {
			Trace::Log("CONFIG", "no encuentro el tema %s", fn.c_str()) ;
			ok = false ;
		}
	}
	for (int i = 0; themeKeys[i]; i++) Set(themeKeys[i], vals[themeKeys[i]].c_str()) ;
	generation_++ ;
	Trace::Log("CONFIG", "tema %s (%s)", name ? name : "", ok ? "ok" : "base") ;
	return ok ;
}


//------------------------------------------------------------------------------

Config::~Config()
{
}


//------------------------------------------------------------------------------

bool Config::SetPersistent(const char *key,const char *value)
{
	Variable *v=FindVariable(key) ;
	if (v) v->SetString(value) ;
	else Insert(new Variable(key,0,value)) ;
	Path path("bin:config.xml") ;
	TiXmlDocument doc(path.GetPath()) ;
	if (!doc.LoadFile()) return false ;
	TiXmlNode *root=doc.FirstChild("CONFIG") ;
	if (!root) root=doc.FirstChild("GPCONFIG") ;
	if (!root) return false ;
	TiXmlElement *e=root->FirstChildElement(key) ;
	if (!e) {
		TiXmlElement add(key) ;
		e=root->InsertEndChild(add)->ToElement() ;
	}
	if (!e) return false ;
	e->SetAttribute("value",value) ;
	bool ok=doc.SaveFile() ;
	Trace::Log("CONFIG","%s=%s guardado: %s",key,value,ok?"si":"no") ;
	return ok ;
}

const char *Config::GetValue(const char *key) 
{
	Variable *v=FindVariable(key) ;
	if (v) {
		Trace::Log("CONFIG","Got value for %s=%s",key,v->GetString()) ;
	}
	return v?v->GetString():0 ;
} ;


//------------------------------------------------------------------------------

void Config::ProcessArguments(int argc,char **argv) 
{
	for (int i=1;i<argc;i++) {
		char *pos ;
		char *arg=argv[i] ;
		while (*arg=='-') arg++ ;
		if ((pos=strchr(arg,'='))!=0) {
			*pos=0 ;
			Variable *v=FindVariable(arg) ;
			if (v) {
				v->SetString(pos+1) ;
			} else {
				Variable *v=new Variable(arg,0,pos+1) ;
				Insert(v) ;
			}
		}
	}
} ;


//------------------------------------------------------------------------------
