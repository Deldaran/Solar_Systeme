#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Body.hpp — Entité corps céleste (SOLID: SRP, OCP)
//  Données pures : contexte, position, vitesse, masse, rayon, apparence
// ════════════════════════════════════════════════════════════════════════

#include <glm/glm.hpp>
#include <string>

struct Body {
    // ── Identité ─────────────────────────────
    std::string  name;

    // ── Contexte (cf. Frame.hpp) ─────────────
    // Index du repère dans lequel pos/vel sont exprimés. Ce ne sont PAS
    // des coordonnées absolues : la Lune est stockée relativement à la
    // Terre, la Terre relativement au Soleil, etc. C'est ce qui rend la
    // précision indépendante de la distance à l'étoile.
    int          frame = 0;

    // ── Physique (CPU float64) ────────────────
    glm::dvec3   pos;          // km,   dans `frame`
    glm::dvec3   vel;          // km/s, dans `frame`
    double       mass;         // kg
    double       radius;       // km (physique réel)

    // ── Apparence GPU ─────────────────────────
    glm::vec3    color;        // teinte identitaire ; base de la palette procédurale
    float        emissive;     // 0 = planète réfléchissante, 1 = étoile
    float        visualScale;  // multiplicateur du rayon pour le GPU

    // ── Surface procédurale (cf. Surface.hpp / SurfaceGLSL.hpp) ────────
    // La palette n'est pas stockée : elle est dérivée sur le GPU de
    // (surfaceType, surfaceSeed, color). Deux flottants suffisent donc.
    int          surfaceType = 0;    // Surface::Type
    float        surfaceSeed = 0.f;

    // Rayon tel que transmis au GPU = radius * visualScale
    float visualRadius() const {
        return static_cast<float>(radius) * visualScale;
    }
};
