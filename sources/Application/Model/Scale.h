// trk2als: las escalas de Ableton Live (las del Push / Live 12), en su orden:
// el índice - 1 es el "Name" de ScaleInformation en el .als. La 0 = sin escala.
#ifndef SCALE_VIEW_H
#define SCALE_VIEW_H

const int scaleCount = 36;
const int scaleNoteCount = 12;
extern const char *scaleNames[scaleCount];
extern const char *scaleNotes[scaleNoteCount];
extern const bool scaleSteps[scaleCount][scaleNoteCount];

// ¿la nota está en la escala con esa raíz? (escala 0: todas)
inline bool InScale(int scale, int root, int note) {
    if (scale <= 0 || scale >= scaleCount) return true;
    return scaleSteps[scale][((note - root) % 12 + 12) % 12];
}

#endif
