#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Body.hpp — Entité corps céleste (SOLID: SRP, OCP)
//  Données pures : position, vitesse, masse, rayon, apparence
// ════════════════════════════════════════════════════════════════════════

#include <glm/glm.hpp>
#include <string>

struct Body {
    // ── Identité ─────────────────────────────
    std::string  name;

    // ── Physique (CPU double) ─────────────────
    glm::dvec3   pos;          // km
    glm::dvec3   vel;          // km/s
    double       mass;         // kg
    double       radius;       // km (physique réel)

    // ── Apparence GPU ─────────────────────────
    glm::vec3    color;
    float        emissive;     // 0 = planète réfléchissante, 1 = étoile
    float        visualScale;  // multiplicateur du rayon pour le GPU (debug/visibilité)

    // ── Accesseur ────────────────────────────
    // Rayon tel que transmis au GPU = radius * visualScale
    float visualRadius() const {
        return static_cast<float>(radius) * visualScale;
    }
};
