#ifndef _PROJECT_NAME_H_
#define _PROJECT_NAME_H_

// trk2als: las carpetas de proyecto. Las nuevas se llaman chancho_<nombre>;
// las de antes (lgpt_<nombre>, lgpt<nombre>) se siguen abriendo igual.

#include <string>
#include <ctype.h>

#define PROJECT_PREFIX "chancho_"

// cuántas letras de prefijo tiene (0 si no es un proyecto)
inline size_t ProjectPrefixLen(const std::string &n) {
    std::string low(n);
    for (size_t i = 0; i < low.size(); i++) low[i] = tolower(low[i]);
    if (low.compare(0, 8, "chancho_") == 0 && n.size() > 8) return 8;
    if (low.compare(0, 4, "lgpt") == 0 && n.size() > 4) {
        if (n[4] == '_') return n.size() > 5 ? 5 : 0;
        return isalnum((unsigned char)n[4]) ? 4 : 0;   // (lgpt.xcodeproj no)
    }
    return 0;
}

inline bool IsProjectFolder(const std::string &n) { return ProjectPrefixLen(n) > 0; }

// el nombre sin el prefijo
inline std::string ProjectNiceName(const std::string &n) {
    size_t p = ProjectPrefixLen(n);
    return p ? n.substr(p) : n;
}

#endif
