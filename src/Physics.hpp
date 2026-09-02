#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Physics.hpp — N-corps Velocity Verlet contextuel (SOLID: SRP)
//
//  Les positions ne sont pas absolues : chaque corps vit dans un contexte
//  (cf. Frame.hpp). Deux conséquences.
//
//  1) Les séparations passent par FrameGraph::separation, qui remonte à
//     l'ancêtre commun. On ne construit JAMAIS de coordonnée absolue, donc
//     jamais de soustraction catastrophique entre deux grands nombres :
//     le vecteur Terre→Lune est lu directement (384 400 km), au lieu
//     d'être obtenu par 1.496e8 − 1.496e8.
//
//  2) Un contexte ancré sur un corps est NON INERTIEL. La position locale
//     p d'un corps vérifie P = p + O, donc
//         d²p/dt² = a_abs(corps) − a_abs(ancre du contexte).
//     Les repères ne tournant pas, il n'y a ni Coriolis ni centrifuge :
//     une simple soustraction suffit, et elle est exacte.
//
//  Velocity Verlet reste symplectique : l'énergie ne dérive pas.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Frame.hpp"
#include "Constants.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace Physics {

// Softening : évite la singularité 1/r² si deux corps se confondent.
constexpr double SOFTENING2 = 1.0;   // km²

// ── Accélérations absolues (mêmes axes pour tous les contextes) ──────────
inline void computeAccelerations(const std::vector<Body>& bodies,
                                 const FrameGraph& fg,
                                 std::vector<glm::dvec3>& accAbs)
{
    const int N = static_cast<int>(bodies.size());
    accAbs.assign(N, glm::dvec3(0.0));
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            // Vecteur i→j résolu par la chaîne de contextes
            glm::dvec3 r = fg.separation(bodies[i].pos, bodies[i].frame,
                                         bodies[j].pos, bodies[j].frame, bodies);
            double dist2 = glm::dot(r, r) + SOFTENING2;
            double inv   = 1.0 / (dist2 * std::sqrt(dist2));   // 1 / r³
            glm::dvec3 d = r * inv;
            accAbs[i] += Constants::G * bodies[j].mass * d;
            accAbs[j] -= Constants::G * bodies[i].mass * d;
        }
    }
}

// Passe des accélérations absolues aux accélérations LOCALES au contexte.
inline void toLocalAccelerations(const std::vector<Body>& bodies,
                                 const FrameGraph& fg,
                                 const std::vector<glm::dvec3>& accAbs,
                                 std::vector<glm::dvec3>& accLocal)
{
    const int N = static_cast<int>(bodies.size());
    accLocal.resize(N);
    for (int i = 0; i < N; ++i) {
        accLocal[i] = accAbs[i];
        int f = bodies[i].frame;
        if (fg.valid(f)) {
            int a = fg.frames[f].anchor;      // corps qui porte l'origine
            if (a >= 0 && a < N) accLocal[i] -= accAbs[a];
        }
    }
}

// ── Intégrateur à état explicite ─────────────────────────────────────────
class Integrator {
public:
    // À appeler après toute modification externe des corps (reset, ajout,
    // changement de contexte) : force le recalcul au pas suivant.
    void invalidate() { m_primed = false; }

    void step(std::vector<Body>& bodies, const FrameGraph& fg, double dt) {
        const int N = static_cast<int>(bodies.size());
        if (!m_primed || static_cast<int>(m_a0.size()) != N) {
            computeAccelerations(bodies, fg, m_abs);
            toLocalAccelerations(bodies, fg, m_abs, m_a0);
            m_primed = true;
        }
        for (int i = 0; i < N; ++i)
            bodies[i].pos += bodies[i].vel * dt + 0.5 * m_a0[i] * dt * dt;

        computeAccelerations(bodies, fg, m_abs);
        toLocalAccelerations(bodies, fg, m_abs, m_a1);

        for (int i = 0; i < N; ++i)
            bodies[i].vel += 0.5 * (m_a0[i] + m_a1[i]) * dt;
        m_a0.swap(m_a1);
    }

private:
    std::vector<glm::dvec3> m_abs, m_a0, m_a1;
    bool                    m_primed = false;
};

// ════════════════════════════════════════════════════════════════════════
//  Diagnostics — travaillent en coordonnées racine (inertielles)
// ════════════════════════════════════════════════════════════════════════

inline glm::dvec3 absolutePos(const Body& b, const FrameGraph& fg,
                              const std::vector<Body>& bodies) {
    return fg.toRoot(b.pos, b.frame, bodies);
}

inline glm::dvec3 absoluteVel(const Body& b, const FrameGraph& fg,
                              const std::vector<Body>& bodies) {
    return b.vel + fg.climbVel(b.frame, -1, bodies);
}

inline glm::dvec3 barycenter(const std::vector<Body>& bodies, const FrameGraph& fg) {
    glm::dvec3 p(0.0); double m = 0.0;
    for (const auto& b : bodies) { p += absolutePos(b, fg, bodies) * b.mass; m += b.mass; }
    return (m > 0.0) ? p / m : glm::dvec3(0.0);
}

inline glm::dvec3 barycenterVelocity(const std::vector<Body>& bodies, const FrameGraph& fg) {
    glm::dvec3 p(0.0); double m = 0.0;
    for (const auto& b : bodies) { p += absoluteVel(b, fg, bodies) * b.mass; m += b.mass; }
    return (m > 0.0) ? p / m : glm::dvec3(0.0);
}

// Annule le moment linéaire total. Il suffit de corriger les corps du frame
// racine : tout ce qui est ancré en dessous suit automatiquement, puisque
// leur vitesse absolue est locale + celle de la chaîne de contextes.
inline void cancelDrift(std::vector<Body>& bodies, const FrameGraph& fg) {
    glm::dvec3 v = barycenterVelocity(bodies, fg);
    for (auto& b : bodies)
        if (fg.valid(b.frame) && fg.frames[b.frame].parent < 0)
            b.vel -= v;
}

// Temps dynamique le plus court : min de sqrt(r³ / G(mi+mj)) sur les paires.
inline double shortestDynamicalTime(const std::vector<Body>& bodies,
                                    const FrameGraph& fg) {
    double best = 1e300;
    const int N = static_cast<int>(bodies.size());
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            double r = glm::length(fg.separation(bodies[i].pos, bodies[i].frame,
                                                 bodies[j].pos, bodies[j].frame, bodies));
            double mu = Constants::G * (bodies[i].mass + bodies[j].mass);
            if (r > 0.0 && mu > 0.0)
                best = std::min(best, std::sqrt(r * r * r / mu));
        }
    return (best < 1e300) ? best : 0.0;
}

// Énergie mécanique totale — l'énergie cinétique se calcule avec les
// vitesses ABSOLUES (inertielles), pas les vitesses locales.
inline double totalEnergy(const std::vector<Body>& bodies, const FrameGraph& fg) {
    double E = 0.0;
    const int N = static_cast<int>(bodies.size());
    for (int i = 0; i < N; ++i) {
        glm::dvec3 v = absoluteVel(bodies[i], fg, bodies);
        E += 0.5 * bodies[i].mass * glm::dot(v, v);
        for (int j = i + 1; j < N; ++j) {
            double d = glm::length(fg.separation(bodies[i].pos, bodies[i].frame,
                                                 bodies[j].pos, bodies[j].frame, bodies));
            if (d > 0.0) E -= Constants::G * bodies[i].mass * bodies[j].mass / d;
        }
    }
    return E;
}

} // namespace Physics
