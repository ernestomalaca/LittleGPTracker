// trk2als: escalas de Ableton Live (ver Scale.h)
#include "Scale.h"

const char *scaleNotes[scaleNoteCount] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

const char *scaleNames[scaleCount] = {
    "Off",
    "Major",
    "Minor",
    "Dorian",
    "Mixolydian",
    "Lydian",
    "Phrygian",
    "Locrian",
    "Whole Tone",
    "Half-whole Dim.",
    "Whole-half Dim.",
    "Minor Blues",
    "Minor Pentatonic",
    "Major Pentatonic",
    "Harmonic Minor",
    "Harmonic Major",
    "Dorian #4",
    "Phrygian Dominant",
    "Melodic Minor",
    "Lydian Augmented",
    "Lydian Dominant",
    "Super Locrian",
    "8-Tone Spanish",
    "Bhairav",
    "Hungarian Minor",
    "Hirajoshi",
    "In-Sen",
    "Iwato",
    "Kumoi",
    "Pelog Selisir",
    "Pelog Tembung",
    "Messiaen 3",
    "Messiaen 4",
    "Messiaen 5",
    "Messiaen 6",
    "Messiaen 7",
};

const bool scaleSteps[scaleCount][scaleNoteCount] = {
    {1,1,1,1,1,1,1,1,1,1,1,1}, // Off
    {1,0,1,0,1,1,0,1,0,1,0,1}, // Major
    {1,0,1,1,0,1,0,1,1,0,1,0}, // Minor
    {1,0,1,1,0,1,0,1,0,1,1,0}, // Dorian
    {1,0,1,0,1,1,0,1,0,1,1,0}, // Mixolydian
    {1,0,1,0,1,0,1,1,0,1,0,1}, // Lydian
    {1,1,0,1,0,1,0,1,1,0,1,0}, // Phrygian
    {1,1,0,1,0,1,1,0,1,0,1,0}, // Locrian
    {1,0,1,0,1,0,1,0,1,0,1,0}, // Whole Tone
    {1,1,0,1,1,0,1,1,0,1,1,0}, // Half-whole Dim.
    {1,0,1,1,0,1,1,0,1,1,0,1}, // Whole-half Dim.
    {1,0,0,1,0,1,1,1,0,0,1,0}, // Minor Blues
    {1,0,0,1,0,1,0,1,0,0,1,0}, // Minor Pentatonic
    {1,0,1,0,1,0,0,1,0,1,0,0}, // Major Pentatonic
    {1,0,1,1,0,1,0,1,1,0,0,1}, // Harmonic Minor
    {1,0,1,0,1,1,0,1,1,0,0,1}, // Harmonic Major
    {1,0,1,1,0,0,1,1,0,1,1,0}, // Dorian #4
    {1,1,0,0,1,1,0,1,1,0,1,0}, // Phrygian Dominant
    {1,0,1,1,0,1,0,1,0,1,0,1}, // Melodic Minor
    {1,0,1,0,1,0,1,0,1,1,0,1}, // Lydian Augmented
    {1,0,1,0,1,0,1,1,0,1,1,0}, // Lydian Dominant
    {1,1,0,1,1,0,1,0,1,0,1,0}, // Super Locrian
    {1,1,0,1,1,1,1,0,1,0,1,0}, // 8-Tone Spanish
    {1,1,0,0,1,1,0,1,1,0,0,1}, // Bhairav
    {1,0,1,1,0,0,1,1,1,0,0,1}, // Hungarian Minor
    {1,0,1,1,0,0,0,1,1,0,0,0}, // Hirajoshi
    {1,1,0,0,0,1,0,1,0,0,1,0}, // In-Sen
    {1,1,0,0,0,1,1,0,0,0,1,0}, // Iwato
    {1,0,1,1,0,0,0,1,0,1,0,0}, // Kumoi
    {1,1,0,1,0,0,0,1,1,0,0,0}, // Pelog Selisir
    {1,1,0,0,0,1,0,1,1,0,0,0}, // Pelog Tembung
    {1,0,1,1,1,0,1,1,1,0,1,1}, // Messiaen 3
    {1,1,1,0,0,1,1,1,1,0,0,1}, // Messiaen 4
    {1,1,0,0,0,1,1,1,0,0,0,1}, // Messiaen 5
    {1,0,1,0,1,1,1,0,1,0,1,1}, // Messiaen 6
    {1,1,1,1,0,1,1,1,1,1,0,1}, // Messiaen 7
};
