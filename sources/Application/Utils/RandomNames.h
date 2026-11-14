#ifndef _RANDOM_NAMES_H_
#define _RANDOM_NAMES_H_

#include <string>
#include <vector>
#include <cstdlib>
#include "time.h"

// trk2als: geografía y clima de Chile...
static const std::string adj[] = {
        "Austral", "Cordillera", "Salar", "Valle", "Andes", "Altiplano",
        "Fiordo", "Archipelago", "Glaciar", "Pampa", "Quebrada", "Meseta",
        "Precordillera", "Peninsula", "Estrecho", "Atacama", "Patagonia",
        "Chiloe", "Aysen", "Magallanes", "Elqui", "Maipo", "Araucania",
        "Biobio", "Maule", "Aconcagua", "RapaNui", "Valparaiso", "Calama",
        "Fuego", "Bruma", "Camanchaca", "Puelche", "Ventisca", "Garua",
        "Escarcha", "Alba", "Ocaso", "Equinoccio", "Solsticio", "Cobre",
        "Basalto", "Obsidiana", "Litio", "Cuarzo", "Araucaria", "Alerce",
        "Calafate", "Copihue", "Humedal"
    };
// ... y el ritmo
static const std::string vrb[] =
    {
        "Beat", "Riddim", "Cueca", "Jam", "Cumbia", "Groove", "Loop",
        "Swing", "Dub", "Pulso", "Ritmo", "Rumba", "Bombo", "Bajo", "Compas",
        "Sonido", "Eco", "Onda", "Tono", "Acorde", "Trance", "House",
        "Techno", "Bass", "Break", "Vibe", "Flow", "Funk", "Soul", "Jazz",
        "Punk", "Ska", "Trap", "Perreo", "Zamba", "Samba", "Tango", "Salsa",
        "Bolero", "Vals", "Tonada", "Ronda", "Fiesta", "Baile", "Danzon",
        "Tumbao", "Marcha", "Toque", "Corte", "Pista"
    };

bool noSeed = true;
//Wonky assignment because < C++11
std::vector<std::string> adjectives_ (adj, adj + sizeof(adj) / sizeof(adj[0]) );
std::vector<std::string> verbs_ (vrb, vrb + sizeof(vrb) / sizeof(vrb[0]) );

// Generate a random name made in the format of: "adjective-verb"
std::string getRandomName() {
    if (noSeed){
        srand(uint(time(NULL)));
        noSeed = false;
    }

    std::string adjective = adjectives_[rand() % adjectives_.size()];
    std::string verb = verbs_[rand() % verbs_.size()];

    while ((adjective + verb).length() > MAX_NAME_LENGTH) {
        adjective = adjectives_[rand() % adjectives_.size()];
        verb = verbs_[rand() % verbs_.size()];
    }

    return (adjective + verb);
}

#endif //_RANDOM_NAMES_H_
