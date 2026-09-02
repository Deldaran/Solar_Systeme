#pragma once

// ════════════════════════════════════════════════════════════════════════
//  BodyFactory.hpp — Fabrique de corps célestes (SOLID: SRP, OCP)
//  Valeurs réelles (NASA planetary fact sheets), unités km / kg / s.
//  Orbites circularisées dans le plan XZ, sens direct.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Constants.hpp"
#include "Physics.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>

namespace BodyFactory {

// Vitesse d'une orbite circulaire autour d'une masse centrale
inline double circularVelocity(double massCentral, double dist) {
    return std::sqrt(Constants::G * massCentral / dist);
}

// ── Étoile ───────────────────────────────────────────────────────────────
inline Body makeSun() {
    Body b;
    b.name        = "Soleil";
    b.pos         = glm::dvec3(0.0);
    b.vel         = glm::dvec3(0.0);
    b.mass        = Constants::MASS_SUN;
    b.radius      = Constants::RADIUS_SUN;
    b.color       = glm::vec3(1.f, 0.85f, 0.2f);
    b.emissive    = 1.f;
    b.visualScale = 1.f;
    return b;
}

// ── Planète en orbite circulaire autour du Soleil ────────────────────────
inline Body makePlanet(const char* name, double distKm, double radiusKm,
                       double massKg, const glm::vec3& color,
                       float visualScale = 1.f)
{
    Body b;
    b.name        = name;
    b.pos         = glm::dvec3(distKm, 0.0, 0.0);
    b.vel         = glm::dvec3(0.0, 0.0, circularVelocity(Constants::MASS_SUN, distKm));
    b.mass        = massKg;
    b.radius      = radiusKm;
    b.color       = color;
    b.emissive    = 0.f;
    b.visualScale = visualScale;
    return b;
}

inline Body makeEarth() {
    return makePlanet("Terre", Constants::DIST_EARTH, Constants::RADIUS_EARTH,
                      Constants::MASS_EARTH, glm::vec3(0.18f, 0.46f, 0.95f));
}

// ── Satellite en orbite autour d'un autre corps ──────────────────────────
// La vitesse est celle du parent + la vitesse circulaire locale.
inline Body makeMoonOf(const Body& parent, const char* name, double distKm,
                       double radiusKm, double massKg, const glm::vec3& color)
{
    Body b;
    b.name        = name;
    b.pos         = parent.pos + glm::dvec3(distKm, 0.0, 0.0);
    b.vel         = parent.vel + glm::dvec3(0.0, 0.0, circularVelocity(parent.mass, distKm));
    b.mass        = massKg;
    b.radius      = radiusKm;
    b.color       = color;
    b.emissive    = 0.f;
    b.visualScale = 1.f;
    return b;
}

// ════════════════════════════════════════════════════════════════════════
//  Système solaire complet
//  Le moment linéaire total est annulé : le barycentre reste fixe, donc
//  le système ne dérive jamais hors de vue.
// ════════════════════════════════════════════════════════════════════════
inline std::vector<Body> makeSolarSystem()
{
    std::vector<Body> v;
    v.push_back(makeSun());

    v.push_back(makePlanet("Mercure",   57.909e6,  2439.7, 3.301e23, {0.62f,0.57f,0.52f}));
    v.push_back(makePlanet("Venus",    108.210e6,  6051.8, 4.867e24, {0.94f,0.85f,0.62f}));
    const Body earth = makeEarth();          // copie : v va réallouer
    v.push_back(earth);
    v.push_back(makeMoonOf(earth, "Lune", 384400.0, 1737.4, 7.342e22, {0.72f,0.72f,0.70f}));
    v.push_back(makePlanet("Mars",     227.956e6,  3389.5, 6.417e23, {0.85f,0.42f,0.24f}));
    v.push_back(makePlanet("Jupiter",  778.479e6, 69911.0, 1.898e27, {0.83f,0.71f,0.55f}));
    v.push_back(makePlanet("Saturne", 1432.041e6, 58232.0, 5.683e26, {0.91f,0.82f,0.62f}));
    v.push_back(makePlanet("Uranus",  2867.043e6, 25362.0, 8.681e25, {0.60f,0.85f,0.89f}));
    v.push_back(makePlanet("Neptune", 4514.953e6, 49528.0, 1.024e26, {0.26f,0.41f,0.88f}));

    Physics::cancelDrift(v);
    return v;
}

} // namespace BodyFactory
