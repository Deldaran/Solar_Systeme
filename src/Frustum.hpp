#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Frustum.hpp — Frustum culling double précision (SOLID: SRP)
//
//  Le test se fait en espace CAMÉRA-RELATIF : la caméra étant à l'origine,
//  tous les plans latéraux passent par zéro et leur terme constant est nul.
//  On teste donc directement les positions relatives déjà résolues par la
//  chaîne de contextes — aucune coordonnée absolue n'entre ici.
// ════════════════════════════════════════════════════════════════════════

#include "Camera.hpp"
#include "Body.hpp"
#include <glm/glm.hpp>
#include <array>
#include <cmath>

class Frustum {
public:
    // Construit les 5 plans (near + 4 latéraux) autour de l'origine.
    void build(const Camera& cam, double aspect) {
        glm::dvec3 fwd = cam.forward();
        glm::dvec3 wup = glm::dvec3(0, 1, 0);
        if (glm::abs(glm::dot(fwd, wup)) > 0.999) wup = glm::dvec3(0, 0, 1);
        glm::dvec3 rgt = glm::normalize(glm::cross(fwd, wup));
        glm::dvec3 up  = glm::cross(rgt, fwd);

        double fovRad = glm::radians(static_cast<double>(cam.fov));
        double halfH  = std::tan(fovRad * 0.5);
        double halfW  = halfH * aspect;

        // Toutes les normales pointent vers l'INTÉRIEUR du frustum.
        m_n[0] = fwd;                                                  // Near
        m_d[0] = -1.0;                                                 //  à 1 km
        m_n[1] = glm::normalize(glm::cross(fwd - rgt * halfW, up));    // Left
        m_n[2] = glm::normalize(glm::cross(up, fwd + rgt * halfW));    // Right
        m_n[3] = glm::normalize(glm::cross(rgt, fwd - up * halfH));    // Bottom
        m_n[4] = glm::normalize(glm::cross(fwd + up * halfH, rgt));    // Top
        m_d[1] = m_d[2] = m_d[3] = m_d[4] = 0.0;   // plans passant par l'origine
    }

    // relCenter : position DÉJÀ relative à la caméra (km, double)
    bool testSphere(const glm::dvec3& relCenter, double radius) const {
        for (int i = 0; i < 5; ++i)
            if (glm::dot(m_n[i], relCenter) + m_d[i] < -radius) return false;
        return true;
    }

private:
    std::array<glm::dvec3, 5> m_n;
    std::array<double, 5>     m_d{};
};
