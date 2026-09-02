#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Camera.hpp — Caméra orbitale avec Floating Origin (SOLID: SRP)
//  - Double précision pour la position monde (CPU)
//  - Float pour la rotation envoyée au GPU
//  - Séparation rotation / translation (évite les erreurs d'arrondi)
// ════════════════════════════════════════════════════════════════════════

#include "Constants.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

class Camera {
public:
    // ── Floating Origin : position absolue (km, double) ──────────────
    glm::dvec3 posWorld = { 0.0, 0.0, Constants::AU_KM * 3.0 };
    glm::dvec3 target   = { 0.0, 0.0, 0.0 };  // point suivi (km, double)

    // ── Paramètres orbitaux ───────────────────────────────────────────
    float theta    = 0.f;                           // azimut (rad)
    float phi      = 0.45f;                         // élévation (rad)
    float distance = float(Constants::AU_KM * 3.0); // distance orbite (km)
    float fov      = 60.f;                          // champ de vue (degrés)

    // ─────────────────────────────────────────────────────────────────
    //  Recalcule posWorld depuis (target, theta, phi, distance)
    //  Appeler après tout changement d'angle ou de cible.
    // ─────────────────────────────────────────────────────────────────
    void updateFromOrbit() {
        double r = static_cast<double>(distance);
        posWorld = target + glm::dvec3(
            r * std::cos(phi) * std::sin(theta),
            r * std::sin(phi),
            r * std::cos(phi) * std::cos(theta)
        );
    }

    // Direction forward (double précision)
    glm::dvec3 forward() const {
        return glm::normalize(target - posWorld);
    }

    // ─────────────────────────────────────────────────────────────────
    //  Matrice de ROTATION pure (float) — sans aucune translation.
    //  Principe : la translation est absorbée dans posRel côté GPU
    //  (Floating Origin), donc la matrice view ne contient QUE la rotation.
    //  Inverse = transposée car la matrice est orthogonale.
    // ─────────────────────────────────────────────────────────────────
    glm::mat4 rotationMatrix() const {
        glm::dvec3 fwd = glm::normalize(target - posWorld);
        glm::dvec3 wup = glm::dvec3(0, 1, 0);
        if (glm::abs(glm::dot(fwd, wup)) > 0.999)
            wup = glm::dvec3(0, 0, 1);
        glm::dvec3 rgt = glm::normalize(glm::cross(fwd, wup));
        glm::dvec3 up  = glm::cross(rgt, fwd);

        // Colonne-major OpenGL : chaque colonne = un axe de base
        return glm::mat4(
            float(rgt.x), float(up.x), float(-fwd.x), 0.f,
            float(rgt.y), float(up.y), float(-fwd.y), 0.f,
            float(rgt.z), float(up.z), float(-fwd.z), 0.f,
            0.f,          0.f,         0.f,           1.f
        );
    }

    // Matrice rotation inverse (rotation vers espace monde depuis espace caméra)
    glm::mat4 invRotationMatrix() const {
        return glm::transpose(rotationMatrix());
    }
};
