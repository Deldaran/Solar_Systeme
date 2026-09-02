#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Navigation.hpp — Déplacement et ciblage de la caméra (SOLID: SRP)
//
//  Le problème du vol libre à ces échelles : une vitesse fixe est soit
//  ridicule (300 ans pour aller de la Terre à Mars), soit inutilisable
//  (on traverse une planète en une frame). La vitesse est donc indexée
//  sur l'ALTITUDE au-dessus de la surface la plus proche : on rampe au
//  ras d'une lune et on traverse le système en quelques secondes, sans
//  jamais changer de réglage.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Frame.hpp"
#include "Camera.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

namespace Navigation {

struct Nearest {
    int    index    = -1;
    double distance = 0.0;   // caméra → centre du corps (km)
    double altitude = 0.0;   // caméra → surface visuelle (km, >= 0)
};

// Corps dont la SURFACE visuelle est la plus proche de la caméra.
inline Nearest nearestBody(const Camera& cam, const std::vector<Body>& bodies,
                           const FrameGraph& fg)
{
    Nearest n;
    double best = 1e300;
    for (int i = 0; i < (int)bodies.size(); ++i) {
        double d = glm::length(fg.separation(cam.pos, cam.frame,
                                             bodies[i].pos, bodies[i].frame,
                                             bodies));
        double alt = d - double(bodies[i].visualRadius());
        if (alt < best) {
            best = alt;
            n.index = i; n.distance = d; n.altitude = std::max(0.0, alt);
        }
    }
    return n;
}

// Vitesse de vol : traverser l'espace libre devant soi en ~4 secondes.
// Plancher à 50 m/s pour le placement fin, plafond pour rester pilotable.
inline double freeFlySpeed(double altitude, float boost)
{
    double v = altitude / 4.0;
    v = std::clamp(v, 0.05, 5.0e7);
    return v * double(boost);
}

// ── Ciblage ─────────────────────────────────────────────────────────────
// Entre dans le contexte du corps `i` et l'installe au centre exact.
//
// La caméra est d'abord REPARENTÉE (translation exacte : elle ne bouge pas
// d'un millimètre), puis la cible devient (0,0,0) — l'origine du contexte
// EST le centre du corps. Le corps reste donc centré pour toujours, sans
// qu'aucune coordonnée n'ait à être recalculée frame après frame.
inline void focusOn(Camera& cam, const FrameGraph& fg,
                    const std::vector<Body>& bodies, int i,
                    bool keepDistance = true)
{
    if (i < 0 || i >= (int)bodies.size()) return;

    int f = fg.frameAnchoredTo(i);
    if (f >= 0) {
        glm::dvec3 dummyVel(0.0);
        int cf = cam.frame;
        fg.reparent(cam.pos, dummyVel, cf, f, bodies);   // ne déplace rien
        cam.frame  = cf;
        cam.target = glm::dvec3(0.0);                    // exactement le centre
    } else {
        cam.target = fg.convert(bodies[i].pos, bodies[i].frame, cam.frame, bodies);
    }

    if (keepDistance) {
        // Reconstruit (theta, phi, distance) depuis la position actuelle :
        // la caméra ne saute pas, elle se contente de viser la cible.
        cam.toOrbit(cam.target);
    } else {
        cam.mode = Camera::Mode::Orbit;
        cam.updateFromOrbit();
    }
}

// Distance de cadrage confortable pour un corps donné.
inline float framingDistance(const Body& b) {
    return float(b.radius * double(b.visualScale) * 6.0);
}

// Distance de zoom minimale : le rayon visuel du corps suivi, pas celui
// du Soleil (sinon impossible d'approcher une planète).
inline float minZoomDistance(const std::vector<Body>& bodies, int followIndex,
                             double fallbackRadius)
{
    if (followIndex >= 0 && followIndex < (int)bodies.size())
        return float(double(bodies[followIndex].visualRadius()) * 1.15);
    return float(fallbackRadius * 1.05);
}

} // namespace Navigation
