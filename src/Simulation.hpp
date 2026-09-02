#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Simulation.hpp — Gestion de la simulation physique (SOLID: SRP)
//  Responsabilité unique : avancer le temps, gérer le trail d'orbite
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Physics.hpp"
#include "Constants.hpp"
#include <vector>
#include <deque>

class Simulation {
public:
    bool   paused        = false;
    double deltaT        = Constants::DAY_S;   // secondes simulées par frame
    int    stepsPerFrame = 10;                 // sous-étapes Verlet par frame
    bool   showTrail     = true;

    double simTime       = 0.0;                // secondes simulées totales

    // Corps tracé par le trail (-1 = aucun). Par défaut la Terre.
    int    trailBody     = 1;

    // Trail : positions absolues (double). deque → push/pop O(1) aux deux
    // bouts, contrairement à vector::erase(begin()) qui était O(n)/frame.
    std::deque<glm::dvec3> trail;
    static constexpr int TRAIL_MAX = 3000;

    // ── Avance d'un frame réel ────────────────────────────────────────
    void step(std::vector<Body>& bodies) {
        if (paused) return;

        double subDt = deltaT / static_cast<double>(stepsPerFrame);
        for (int s = 0; s < stepsPerFrame; ++s) {
            m_integrator.step(bodies, subDt);
            simTime += subDt;
        }

        if (showTrail && trailBody >= 0 && trailBody < (int)bodies.size()) {
            trail.push_back(bodies[trailBody].pos);
            while (static_cast<int>(trail.size()) > TRAIL_MAX)
                trail.pop_front();
        }
    }

    void reset(std::vector<Body>& bodies,
               const std::vector<Body>& initialBodies) {
        bodies  = initialBodies;
        trail.clear();
        simTime = 0.0;
        m_integrator.invalidate();   // sinon on réutilise les acc. d'avant
    }

    // À appeler si les corps sont modifiés hors du pas de temps
    void invalidate() { m_integrator.invalidate(); }

    // Pas de temps effectif d'une sous-étape (secondes) — sert au garde-fou UI
    double subStepSeconds() const {
        return deltaT / static_cast<double>(stepsPerFrame > 0 ? stepsPerFrame : 1);
    }

private:
    Physics::Integrator m_integrator;
};
