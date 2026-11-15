#include "Tiny2NosStub.h"
#include "System/Console/Trace.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void fprintf(I_File *f,char *fmt,...) {
     // trk2als: antes un búfer fijo de 256 con vsprintf: un atributo largo (las
     // zonas de un soundfont, nombres de chains...) lo desbordaba y el tracker
     // moría al guardar. Ahora se mide primero y, si no cabe, se pide memoria
     char small[256] ;
     va_list args;
     va_start(args,fmt);
     int len=vsnprintf(small,sizeof(small),fmt,args) ;
     va_end(args) ;
     if (len<0) return ;
     if (len<(int)sizeof(small)) {
         f->Write(small,1,len) ;
         return ;
     }
     char *big=(char *)malloc(len+1) ;
     if (!big) return ;
     va_start(args,fmt);
     vsnprintf(big,len+1,fmt,args) ;
     va_end(args) ;
     f->Write(big,1,len) ;
     free(big) ;
} ;
