#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Camera.hpp — Caméra orbitale contextuelle (SOLID: SRP)
//
//  La caméra vit dans un contexte, comme tout le reste. Au rendu elle est
//  à l'origine : les corps lui sont envoyés relativement à elle, résolus
//  par la chaîne de contextes en float64 puis convertis en float.
//
//  Les contextes ne tournent pas (translations pures), donc la matrice de
//  rotation se calcule identiquement dans n'importe quel contexte.
// ════════════════════════════════════════════════════════════════════════

#include "Constants.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

class Camera {
public:
    // ── Contexte courant ──────────────────────────────────────────────
    int        frame  = 0;

    // ── Position et cible, LOCALES à `frame` (km, double) ─────────────
    glm::dvec3 pos    = { 0.0, 0.0, Constants::AU_KM * 3.0 };
    glm::dvec3 target = { 0.0, 0.0, 0.0 };

    // ── Paramètres orbitaux ───────────────────────────────────────────
    float theta    = 0.f;                           // azimut (rad)
    float phi      = 0.45f;                         // élévation (rad)
    float distance = float(Constants::AU_KM * 3.0); // distance orbite (km)
    float fov      = 60.f;                          // champ de vue (degrés)

    // Recalcule pos depuis (target, theta, phi, distance)
    void updateFromOrbit() {
        double r = static_cast<double>(distance);
        pos = target + glm::dvec3(
            r * std::cos(phi) * std::sin(theta),
            r * std::sin(phi),
            r * std::cos(phi) * std::cos(theta)
        );
    }

    glm::dvec3 forward() const {
        return glm::normalize(target - pos);
    }

    // ─────────────────────────────────────────────────────────────────
    //  Matrice de ROTATION pure (float) — sans aucune translation.
    //  La translation est absorbée côté CPU en double : la matrice envoyée
    //  au GPU ne contient QUE la rotation. Inverse = transposée
    //  (matrice orthogonale).
    // ─────────────────────────────────────────────────────────────────
    glm::mat4 rotationMatrix() const {
        glm::dvec3 fwd = glm::normalize(target - pos);
        glm::dvec3 wup = glm::dvec3(0, 1, 0);
        if (glm::abs(glm::dot(fwd, wup)) > 0.999)
            wup = glm::dvec3(0, 0, 1);
        glm::dvec3 rgt = glm::normalize(glm::cross(fwd, wup));
        glm::dvec3 up  = glm::cross(rgt, fwd);

        // Column-major OpenGL : chaque groupe de 4 = une colonne
        return glm::mat4(
            float(rgt.x), float(up.x), float(-fwd.x), 0.f,
            float(rgt.y), float(up.y), float(-fwd.y), 0.f,
            float(rgt.z), float(up.z), float(-fwd.z), 0.f,
            0.f,          0.f,         0.f,           1.f
        );
    }

    glm::mat4 invRotationMatrix() const {
        return glm::transpose(rotationMatrix());
    }
};
