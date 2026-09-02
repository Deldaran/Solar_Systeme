#pragma once

// ════════════════════════════════════════════════════════════════════════
//  BodyFactory.hpp — Fabrique du système (SOLID: SRP, OCP)
//  Valeurs réelles (NASA planetary fact sheets), unités km / kg / s.
//
//  Construit conjointement les corps ET l'arbre de contextes :
//
//      racine (barycentre, inertiel)
//        └── Soleil ──┬── Mercure
//                     ├── Venus
//                     ├── Terre ── Lune
//                     ├── Mars … Neptune
//
//  Chaque planète porte son propre contexte, avec sa sphère d'influence.
//  Une planète est stockée relativement au Soleil, la Lune relativement à
//  la Terre : la vitesse de la Lune est simplement sa vitesse orbitale
//  locale (1.02 km/s), plus besoin d'y ajouter celle de la Terre.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Frame.hpp"
#include "Surface.hpp"
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

// ── Le système complet : corps + contextes ───────────────────────────────
struct System {
    std::vector<Body> bodies;
    FrameGraph        frames;

    int rootFrame = 0;
    int sunFrame  = 1;
};

inline Body makeSun() {
    Body b;
    b.name        = "Soleil";
    b.frame       = 0;                 // racine
    b.pos         = glm::dvec3(0.0);
    b.vel         = glm::dvec3(0.0);
    b.mass        = Constants::MASS_SUN;
    b.radius      = Constants::RADIUS_SUN;
    b.color       = glm::vec3(1.f, 0.85f, 0.2f);
    b.emissive    = 1.f;
    b.visualScale = 1.f;
    b.surfaceSeed = Surface::seedFromName("Soleil");
    return b;
}

// Corps en orbite circulaire dans le contexte `frame`, autour d'une ancre
// de masse `centralMass`. pos et vel sont LOCAUX à ce contexte.
inline Body makeOrbiter(const char* name, int frame, double distKm,
                        double radiusKm, double massKg, double centralMass,
                        const glm::vec3& color,
                        Surface::Type surface = Surface::Type::Desert)
{
    Body b;
    b.name        = name;
    b.frame       = frame;
    b.pos         = glm::dvec3(distKm, 0.0, 0.0);
    b.vel         = glm::dvec3(0.0, 0.0, circularVelocity(centralMass, distKm));
    b.mass        = massKg;
    b.radius      = radiusKm;
    b.color       = color;
    b.emissive    = 0.f;
    b.visualScale = 1.f;
    b.surfaceType = (int)surface;
    b.surfaceSeed = Surface::seedFromName(name);
    return b;
}

inline System makeSolarSystem()
{
    System s;
    auto& B = s.bodies;
    auto& F = s.frames;

    // ── Racine : barycentre du système, inertiel ──────────────────────
    s.rootFrame = F.addRoot("Barycentre");

    // ── Le Soleil vit dans la racine ; il porte le contexte héliocentrique
    B.push_back(makeSun());
    const int sunBody = 0;
    s.sunFrame = F.add("Soleil", s.rootFrame, sunBody,
                       0.0, 0.0, 0.0);          // SOI infinie (0 = pas de test)

    // ── Planètes : stockées relativement au Soleil ────────────────────
    struct OrbiterDef {
        const char*  name;
        double       a, radius, mass;
        glm::vec3    color;
        Surface::Type surface;
    };
    const OrbiterDef planets[] = {
        { "Mercure",   57.909e6,  2439.7, 3.301e23, {0.62f,0.57f,0.52f}, Surface::Type::Desert      },
        { "Venus",    108.210e6,  6051.8, 4.867e24, {0.94f,0.85f,0.62f}, Surface::Type::Desert      },
        { "Terre",    149.598e6,  6371.0, 5.972e24, {0.18f,0.46f,0.95f}, Surface::Type::Terrestrial },
        { "Mars",     227.956e6,  3389.5, 6.417e23, {0.85f,0.42f,0.24f}, Surface::Type::Desert      },
        { "Jupiter",  778.479e6, 69911.0, 1.898e27, {0.83f,0.71f,0.55f}, Surface::Type::GasGiant    },
        { "Saturne", 1432.041e6, 58232.0, 5.683e26, {0.91f,0.82f,0.62f}, Surface::Type::GasGiant    },
        { "Uranus",  2867.043e6, 25362.0, 8.681e25, {0.60f,0.85f,0.89f}, Surface::Type::GasGiant    },
        { "Neptune", 4514.953e6, 49528.0, 1.024e26, {0.26f,0.41f,0.88f}, Surface::Type::GasGiant    },
    };

    int frameOf[8] = { -1,-1,-1,-1,-1,-1,-1,-1 };
    int k = 0;
    for (const auto& p : planets) {
        int idx = (int)B.size();
        B.push_back(makeOrbiter(p.name, s.sunFrame, p.a, p.radius, p.mass,
                                Constants::MASS_SUN, p.color, p.surface));
        // Chaque planète porte son contexte, avec sa sphère d'influence
        frameOf[k++] = F.add(p.name, s.sunFrame, idx, p.a, p.mass, Constants::MASS_SUN);
    }
    const int fEarth = frameOf[2], fJupiter = frameOf[4], fSaturn = frameOf[5];

    // ── Satellites : stockés dans le contexte de leur planète ─────────
    // Leur vitesse est purement locale — la Lune vaut 1.02 km/s, pas
    // 29.8 + 1.02 : la vitesse de la Terre est portée par le contexte.
    struct MoonDef {
        const char*  name;
        int          frame;
        double       a, radius, mass;
        double       parentMass;
        glm::vec3    color;
        Surface::Type surface;
    };
    const MoonDef moons[] = {
        { "Lune",   fEarth,    384400.0, 1737.4, 7.342e22, Constants::MASS_EARTH,
          {0.72f,0.72f,0.70f}, Surface::Type::Desert },
        { "Europe", fJupiter,  671100.0, 1560.8, 4.800e22, 1.898e27,
          {0.86f,0.88f,0.92f}, Surface::Type::Icy    },
        { "Titan",  fSaturn,  1221870.0, 2574.7, 1.345e23, 5.683e26,
          {0.85f,0.62f,0.30f}, Surface::Type::Icy    },
    };
    for (const auto& m : moons) {
        int idx = (int)B.size();
        B.push_back(makeOrbiter(m.name, m.frame, m.a, m.radius, m.mass,
                                m.parentMass, m.color, m.surface));
        F.add(m.name, m.frame, idx, m.a, m.mass, m.parentMass);
    }

    Physics::cancelDrift(B, F);
    return s;
}

} // namespace BodyFactory
