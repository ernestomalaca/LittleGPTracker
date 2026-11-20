#ifndef _LATIN1_H_
#define _LATIN1_H_

// trk2als: los textos del código están en UTF-8; la letra de la pantalla
// tiene 256 caracteres (Latin-1: tildes, ñ, ¿, ¡, ü). Esto convierte uno en
// el otro. Lo que no está en Latin-1 sale como '?'; un byte suelto que no es
// UTF-8 válido se deja tal cual (los íconos del Piggy, por ejemplo).

#include <string>

inline std::string ToLatin1(const char *s) {
    std::string out;
    if (!s) return out;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char c = *p;
        if (c < 0x80) { out += (char)c; p++; continue; }
        if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            unsigned cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
            out += cp < 256 ? (char)cp : '?';
            p += 2;
            continue;
        }
        if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            out += '?'; p += 3; continue;
        }
        if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
            (p[3] & 0xC0) == 0x80) {
            out += '?'; p += 4; continue;
        }
        out += (char)c; p++;
    }
    return out;
}

inline std::string ToLatin1(const std::string &s) { return ToLatin1(s.c_str()); }

// cuántas letras se ven (no cuántos bytes)
inline int TextLen(const char *s) { return (int)ToLatin1(s).size(); }
inline int TextLen(const std::string &s) { return (int)ToLatin1(s).size(); }

// mayúsculas, también con tilde y ñ (UTF-8 -> UTF-8)
inline std::string UpperUtf8(const std::string &s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); i++) {
        unsigned char c = out[i];
        if (c >= 'a' && c <= 'z') out[i] = char(c - 32);
        else if (c == 0xC3 && i + 1 < out.size()) {
            unsigned char d = out[i + 1];
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7) out[i + 1] = char(d - 0x20);
            i++;
        }
    }
    return out;
}

// para la letra vieja de 8x8 (128 caracteres): la letra sin tilde
inline unsigned char FoldLatin1(unsigned char c) {
    if (c < 0x80) return c;
    static const char *from = "\xE1\xE9\xED\xF3\xFA\xF1\xFC\xC1\xC9\xCD\xD3\xDA\xD1\xDC\xBF\xA1\xE0\xE8\xEC\xF2\xF9\xC0\xC8\xCC\xD2\xD9\xE7\xC7";
    static const char *to = "aeiounuAEIOUNU?!aeiouAEIOUcC";
    for (int i = 0; from[i]; i++)
        if ((unsigned char)from[i] == c) return to[i];
    return '?';
}

#endif
