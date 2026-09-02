#pragma once

// ════════════════════════════════════════════════════════════════════════
//  VisualScale.hpp — Dimensionnement visuel des corps (SOLID: SRP)
//
//  Un système solaire réel est invisible : la Terre vue de 3 UA couvre
//  2e-5 rad, soit 1/300 de pixel. Il faut donc grossir — mais grossir
//  linéairement casse tout : à x800 la Terre fait 5.1e6 km de rayon, soit
//  7 fois le Soleil, et la Lune (384 400 km) disparaît à l'intérieur.
//
//  Trois régimes, chacun cohérent avec un usage :
//
//    Reel        1:1. La vérité physique. À explorer à la caméra libre.
//    Coherent    Chaque corps grossi au maximum SANS jamais empiéter sur
//                son voisin le plus proche. La Lune reste dehors.
//    Schematique Loi de puissance compressive r_vis = K * r^0.5 : la
//                hiérarchie est préservée (le Soleil reste le plus gros,
//                Jupiter devant Mercure) tout en rendant les petits corps
//                visibles de loin. C'est une carte, pas une photo.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Frame.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace VisualScale {

enum class Mode { Reel, Coherent, Schematique };

inline const char* modeName(Mode m) {
    switch (m) {
        case Mode::Reel:        return "Reel (1:1)";
        case Mode::Coherent:    return "Coherent (sans chevauchement)";
        case Mode::Schematique: return "Schematique (hierarchie preservee)";
    }
    return "?";
}

// ── 1:1 ──────────────────────────────────────────────────────────────────
inline void applyReal(std::vector<Body>& bodies) {
    for (auto& b : bodies) b.visualScale = 1.f;
}

// ── Cohérent ─────────────────────────────────────────────────────────────
// UN SEUL facteur, commun à tous les corps : les proportions réelles sont
// donc exactement préservées (le Soleil reste 109 fois la Terre). Le facteur
// est le plus grand qui ne fasse se toucher aucune paire — en pratique il
// est fixé par le couple Terre-Lune, le plus serré du système.
//
// Un facteur PAR CORPS serait une erreur : le voisin le plus proche
// d'Uranus étant à 1.1e9 km, il grossirait 22 000x pendant que la Terre
// resterait à 15x, et la hiérarchie des tailles n'aurait plus aucun sens.
inline float coherentFactor(const std::vector<Body>& bodies,
                            const FrameGraph& fg, double margin = 0.8)
{
    const int N = (int)bodies.size();
    double best = 1e300;
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            double sum = bodies[i].radius + bodies[j].radius;
            if (sum <= 0.0) continue;
            double d = glm::length(fg.separation(bodies[i].pos, bodies[i].frame,
                                                 bodies[j].pos, bodies[j].frame,
                                                 bodies));
            best = std::min(best, d / sum);
        }
    if (best >= 1e300) return 1.f;
    return float(std::max(1.0, best * margin));
}

inline void applyCoherent(std::vector<Body>& bodies, const FrameGraph& fg)
{
    float s = coherentFactor(bodies, fg);
    for (auto& b : bodies) b.visualScale = s;
}

// ── Schématique ──────────────────────────────────────────────────────────
// r_visuel = K * sqrt(r_reel), donc visualScale = K / sqrt(r_reel).
// L'exposant 0.5 comprime la dynamique (le Soleil est 109x la Terre en
// réel, 10.4x ici) sans jamais inverser l'ordre des tailles.
// K = 0 redonne l'échelle réelle.
inline void applySchematic(std::vector<Body>& bodies, float K)
{
    for (auto& b : bodies) {
        if (b.radius <= 0.0) { b.visualScale = 1.f; continue; }
        double s = double(K) / std::sqrt(b.radius);
        b.visualScale = float(std::max(1.0, s));
    }
}

// Valeur de K rendant la Terre lisible depuis quelques UA.
constexpr float K_DEFAULT = 38000.f;

inline void apply(std::vector<Body>& bodies, const FrameGraph& fg,
                  Mode mode, float K)
{
    switch (mode) {
        case Mode::Reel:        applyReal(bodies);              break;
        case Mode::Coherent:    applyCoherent(bodies, fg);      break;
        case Mode::Schematique: applySchematic(bodies, K);      break;
    }
}

} // namespace VisualScale
