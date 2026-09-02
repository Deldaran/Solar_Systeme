#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Constants.hpp — Constantes physiques & unités (SOLID: SRP)
//  Toutes les unités : km, kg, secondes
// ════════════════════════════════════════════════════════════════════════

namespace Constants {

// ── Gravitation ──────────────────────────────
constexpr double G        = 6.674e-20;   // km³ / (kg·s²)

// ── Temps ────────────────────────────────────
constexpr double DAY_S    = 86400.0;     // secondes / jour
constexpr double YEAR_S   = 365.25 * DAY_S;

// ── Distances ────────────────────────────────
constexpr double AU_KM    = 1.496e8;     // 1 UA en km

// ── Soleil ───────────────────────────────────
constexpr double RADIUS_SUN  = 695700.0;  // km
constexpr double MASS_SUN    = 1.989e30;  // kg

// ── Terre ────────────────────────────────────
constexpr double RADIUS_EARTH = 6371.0;   // km
constexpr double MASS_EARTH   = 5.972e24; // kg
constexpr double DIST_EARTH   = AU_KM;    // km

// ── GPU ──────────────────────────────────────
constexpr int MAX_BODIES = 32;

} // namespace Constants
