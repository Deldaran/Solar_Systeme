#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Simulation.hpp — Gestion de la simulation physique (SOLID: SRP)
//  Responsabilité unique : avancer le temps, gérer le trail d'orbite.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Frame.hpp"
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

    double simTime       = 0.0;

    // ── Trail ─────────────────────────────────────────────────────────
    // Les points sont stockés dans le CONTEXTE du corps tracé, pas en
    // absolu : l'orbite de la Terre se referme donc exactement dans le
    // repère du Soleil, et celle de la Lune dans le repère de la Terre.
    int                    trailBody  = -1;
    int                    trailFrame = 0;
    std::deque<glm::dvec3> trail;
    static constexpr int   TRAIL_MAX  = 3000;

    void setTrailBody(int idx, const std::vector<Body>& bodies) {
        trailBody = idx;
        trail.clear();
        if (idx >= 0 && idx < (int)bodies.size()) trailFrame = bodies[idx].frame;
    }

    // ── Avance d'un frame réel ────────────────────────────────────────
    void step(std::vector<Body>& bodies, const FrameGraph& fg) {
        if (paused) return;

        double subDt = deltaT / static_cast<double>(stepsPerFrame);
        for (int s = 0; s < stepsPerFrame; ++s) {
            m_integrator.step(bodies, fg, subDt);
            simTime += subDt;
        }

        if (showTrail && trailBody >= 0 && trailBody < (int)bodies.size()) {
            const Body& b = bodies[trailBody];
            if (b.frame != trailFrame) {   // le corps a changé de contexte
                trail.clear();
                trailFrame = b.frame;
            }
            trail.push_back(b.pos);
            while (static_cast<int>(trail.size()) > TRAIL_MAX)
                trail.pop_front();
        }
    }

    void reset(std::vector<Body>& bodies, const std::vector<Body>& initialBodies) {
        bodies  = initialBodies;
        trail.clear();
        simTime = 0.0;
        m_integrator.invalidate();
    }

    void invalidate() { m_integrator.invalidate(); }

    double subStepSeconds() const {
        return deltaT / static_cast<double>(stepsPerFrame > 0 ? stepsPerFrame : 1);
    }

private:
    Physics::Integrator m_integrator;
};
