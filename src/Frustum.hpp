#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Frustum.hpp — Frustum Culling double précision (SOLID: SRP)
//  Tous les tests se font en km (double) côté CPU.
//  Évite d'envoyer au GPU des corps complètement hors champ.
// ════════════════════════════════════════════════════════════════════════

#include "Camera.hpp"
#include "Body.hpp"
#include <glm/glm.hpp>
#include <array>

struct FrustumPlane {
    glm::dvec3 normal;
    double     d = 0.0;
};

class Frustum {
public:
    // Construit depuis la caméra et le ratio d'aspect
    void build(const Camera& cam, double aspect) {
        glm::dvec3 pos = cam.posWorld;
        glm::dvec3 fwd = cam.forward();
        glm::dvec3 wup = glm::dvec3(0, 1, 0);
        if (glm::abs(glm::dot(fwd, wup)) > 0.999) wup = glm::dvec3(0, 0, 1);
        glm::dvec3 rgt = glm::normalize(glm::cross(fwd, wup));
        glm::dvec3 up  = glm::cross(rgt, fwd);

        double fovRad = glm::radians(static_cast<double>(cam.fov));
        double halfH  = std::tan(fovRad * 0.5);
        double halfW  = halfH * aspect;

        // Near
        m_planes[0].normal = fwd;
        m_planes[0].d      = -glm::dot(fwd, pos + fwd * 1.0);
        // Left
        m_planes[1].normal = glm::normalize(glm::cross(fwd - rgt * halfW, up));
        m_planes[1].d      = -glm::dot(m_planes[1].normal, pos);
        // Right
        m_planes[2].normal = glm::normalize(glm::cross(up, fwd + rgt * halfW));
        m_planes[2].d      = -glm::dot(m_planes[2].normal, pos);
        // Bottom — la normale doit pointer vers l'INTÉRIEUR (vers +up).
        m_planes[3].normal = glm::normalize(glm::cross(rgt, fwd - up * halfH));
        m_planes[3].d      = -glm::dot(m_planes[3].normal, pos);
        // Top — normale vers l'intérieur (vers -up).
        m_planes[4].normal = glm::normalize(glm::cross(fwd + up * halfH, rgt));
        m_planes[4].d      = -glm::dot(m_planes[4].normal, pos);
    }

    // Retourne true si la sphère est (partiellement) dans le frustum
    bool testSphere(const glm::dvec3& center, double radius) const {
        for (int i = 0; i < 5; ++i) {
            double dist = glm::dot(m_planes[i].normal, center) + m_planes[i].d;
            if (dist < -radius) return false;
        }
        return true;
    }

    // Culling complet : filtre un tableau de Body.
    //
    // IMPORTANT : les corps émissifs (étoiles) ne sont JAMAIS cullés. Le
    // fragment shader cherche sa source de lumière parmi les corps qu'on lui
    // envoie ; retirer le Soleil parce qu'il est hors champ rendrait toutes
    // les planètes noires et supprimerait le halo solaire.
    std::vector<const Body*> cull(const std::vector<Body>& bodies) const {
        std::vector<const Body*> visible;
        visible.reserve(bodies.size());
        for (const auto& b : bodies) {
            if (b.emissive > 0.5f || testSphere(b.pos, b.radius * b.visualScale))
                visible.push_back(&b);
        }
        return visible;
    }

private:
    std::array<FrustumPlane, 5> m_planes;
};
