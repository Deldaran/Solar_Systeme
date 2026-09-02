#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Surface.hpp — Catégories de surface procédurale (SOLID: SRP, OCP)
//
//  Le CPU n'envoie que deux nombres par corps : la catégorie et la graine.
//  Toute la palette est dérivée côté GPU (cf. SurfaceGLSL.hpp) à partir de
//  ces deux valeurs et de la couleur identitaire du corps.
//
//  Conséquence voulue : deux planètes désertiques partagent la famille
//  chromatique de leur couleur de base, mais leur graine leur donne des
//  dunes, canyons, cratères et plaques d'oxyde entièrement différents.
//
//  Ajouter une catégorie = une valeur ici + une fonction dans le shader.
// ════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace Surface {

enum class Type : int {
    Terrestrial = 0,   // océans, continents, biomes, calottes — la vie est possible
    Desert      = 1,   // aride : dunes, canyons, oxydes, calottes de CO2
    Icy         = 2,   // banquise fracturée, crevasses, dépôts organiques
    GasGiant    = 3,   // bandes zonales, turbulence, tourbillons
};

constexpr int COUNT = 4;

inline const char* name(Type t) {
    switch (t) {
        case Type::Terrestrial: return "Tellurique (habitable)";
        case Type::Desert:      return "Desertique (aride)";
        case Type::Icy:         return "Glacee";
        case Type::GasGiant:    return "Geante gazeuse";
    }
    return "?";
}

// Libellés dans l'ordre de l'énumération, pour les combos ImGui
inline const char* const* names() {
    static const char* k[COUNT] = {
        "Tellurique (habitable)",
        "Desertique (aride)",
        "Glacee",
        "Geante gazeuse",
    };
    return k;
}

// Graine reproductible dérivée d'un nom : deux exécutions donnent la même
// planète, mais deux corps différents ne se ressemblent jamais.
inline float seedFromName(const char* s) {
    uint32_t h = 2166136261u;                 // FNV-1a
    for (const char* p = s; *p; ++p) {
        h ^= static_cast<uint32_t>(*p);
        h *= 16777619u;
    }
    return float(h % 100000u) * 0.01f;        // 0 .. 1000
}

} // namespace Surface
