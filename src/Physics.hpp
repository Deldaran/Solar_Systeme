#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Physics.hpp — Intégrateur N-corps Velocity Verlet (SOLID: SRP)
//
//  Velocity Verlet est symplectique : l'énergie orbitale ne dérive pas,
//  même sur des milliers d'orbites (contrairement à Euler ou RK4).
//
//  L'état d'accélération est porté par un Integrator explicite, pas par
//  des variables `static` : sinon un Reset réutiliserait silencieusement
//  les accélérations de la simulation précédente, et deux simulations
//  concurrentes se marcheraient dessus.
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Constants.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace Physics {

// Softening : évite la singularité 1/r² quand deux corps se confondent.
// 1 km est très en dessous de tout rayon planétaire → aucun effet visible
// sur les orbites réelles, mais empêche un NaN de contaminer le système.
constexpr double SOFTENING2 = 1.0;   // km²

inline void computeAccelerations(const std::vector<Body>& bodies,
                                 std::vector<glm::dvec3>& acc)
{
    const int N = static_cast<int>(bodies.size());
    acc.assign(N, glm::dvec3(0.0));
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            glm::dvec3 r     = bodies[j].pos - bodies[i].pos;
            double     dist2 = glm::dot(r, r) + SOFTENING2;
            double     inv   = 1.0 / (dist2 * std::sqrt(dist2));  // 1 / r³
            glm::dvec3 dir   = r * inv;
            acc[i] +=  Constants::G * bodies[j].mass * dir;
            acc[j] -=  Constants::G * bodies[i].mass * dir;
        }
    }
}

// ── Intégrateur à état explicite ─────────────────────────────────────────
class Integrator {
public:
    // À appeler après toute modification externe des corps (reset, ajout,
    // téléportation) : force le recalcul des accélérations au pas suivant.
    void invalidate() { m_primed = false; }

    // Un pas Velocity Verlet — dt en secondes
    void step(std::vector<Body>& bodies, double dt) {
        const int N = static_cast<int>(bodies.size());
        if (!m_primed || static_cast<int>(m_acc0.size()) != N) {
            computeAccelerations(bodies, m_acc0);
            m_primed = true;
        }
        for (int i = 0; i < N; ++i)
            bodies[i].pos += bodies[i].vel * dt + 0.5 * m_acc0[i] * dt * dt;
        computeAccelerations(bodies, m_acc1);
        for (int i = 0; i < N; ++i)
            bodies[i].vel += 0.5 * (m_acc0[i] + m_acc1[i]) * dt;
        m_acc0.swap(m_acc1);
    }

private:
    std::vector<glm::dvec3> m_acc0, m_acc1;
    bool                    m_primed = false;
};

// ── Diagnostics ──────────────────────────────────────────────────────────

// Vitesse du barycentre. Si elle est non nulle, tout le système dérive.
inline glm::dvec3 barycenterVelocity(const std::vector<Body>& bodies) {
    glm::dvec3 p(0.0); double m = 0.0;
    for (const auto& b : bodies) { p += b.vel * b.mass; m += b.mass; }
    return (m > 0.0) ? p / m : glm::dvec3(0.0);
}

inline glm::dvec3 barycenter(const std::vector<Body>& bodies) {
    glm::dvec3 p(0.0); double m = 0.0;
    for (const auto& b : bodies) { p += b.pos * b.mass; m += b.mass; }
    return (m > 0.0) ? p / m : glm::dvec3(0.0);
}

// Annule le moment linéaire total : le barycentre reste fixe pour toujours.
// Sans ça, donner à la Terre sa vitesse orbitale sans compenser sur le
// Soleil fait dériver tout le système à ~8.9e-5 km/s (0.0019 UA / siècle).
inline void cancelDrift(std::vector<Body>& bodies) {
    glm::dvec3 v = barycenterVelocity(bodies);
    for (auto& b : bodies) b.vel -= v;
}

// Temps dynamique le plus court du système : min sur toutes les paires de
// sqrt(r³ / G(m_i+m_j)), c'est-à-dire P/2π de l'orbite la plus serrée.
// Intégrer avec un pas comparable à ce temps fait diverger l'orbite ; on
// s'en sert pour avertir l'utilisateur dans l'UI.
inline double shortestDynamicalTime(const std::vector<Body>& bodies) {
    double best = 1e300;
    const int N = static_cast<int>(bodies.size());
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            double r  = glm::length(bodies[j].pos - bodies[i].pos);
            double mu = Constants::G * (bodies[i].mass + bodies[j].mass);
            if (r > 0.0 && mu > 0.0)
                best = std::min(best, std::sqrt(r * r * r / mu));
        }
    return (best < 1e300) ? best : 0.0;
}

// Énergie mécanique totale — doit rester constante (contrôle du pas de temps)
inline double totalEnergy(const std::vector<Body>& bodies) {
    double E = 0.0;
    const int N = static_cast<int>(bodies.size());
    for (int i = 0; i < N; ++i) {
        E += 0.5 * bodies[i].mass * glm::dot(bodies[i].vel, bodies[i].vel);
        for (int j = i + 1; j < N; ++j) {
            double d = glm::length(bodies[j].pos - bodies[i].pos);
            if (d > 0.0)
                E -= Constants::G * bodies[i].mass * bodies[j].mass / d;
        }
    }
    return E;
}

} // namespace Physics
