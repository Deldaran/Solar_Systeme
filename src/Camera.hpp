#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Camera.hpp — Caméra contextuelle à deux modes (SOLID: SRP)
//
//    Orbite : tourne autour d'une cible. La cible d'un corps suivi vaut
//             exactement (0,0,0) dans SON contexte, donc le corps est au
//             centre exact de l'écran, indéfiniment et sans dérive.
//    Libre  : vol 6 axes (WASD + souris), pour se balader dans le système.
//
//  La caméra vit dans un contexte, comme tout le reste. Au rendu elle est
//  à l'origine. Les contextes ne tournent pas (translations pures), donc
//  la matrice de rotation se calcule identiquement partout.
// ════════════════════════════════════════════════════════════════════════

#include "Constants.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

class Camera {
public:
    enum class Mode { Orbit, Free };

    Mode       mode   = Mode::Orbit;

    // ── Contexte courant ──────────────────────────────────────────────
    int        frame  = 0;

    // ── Position et cible, LOCALES à `frame` (km, double) ─────────────
    glm::dvec3 pos    = { 0.0, 0.0, Constants::AU_KM * 3.0 };
    glm::dvec3 target = { 0.0, 0.0, 0.0 };

    // ── Mode orbite ───────────────────────────────────────────────────
    float theta    = 0.f;                           // azimut (rad)
    float phi      = 0.45f;                         // élévation (rad)
    float distance = float(Constants::AU_KM * 3.0); // rayon d'orbite (km)

    // ── Mode libre ────────────────────────────────────────────────────
    float yaw      = glm::pi<float>();              // lacet (rad)
    float pitch    = -0.45f;                        // tangage (rad)
    float speedBoost = 1.f;                         // multiplicateur utilisateur

    float fov      = 60.f;                          // champ de vue (degrés)

    // ═════════════════════════════════════════════════════════════════
    //  Mise à jour
    // ═════════════════════════════════════════════════════════════════

    // Mode orbite : recalcule pos depuis (target, theta, phi, distance).
    //
    // Les angles sont promus en double AVANT l'appel trigonométrique :
    // std::cos(float) sélectionne la surcharge float et rend un résultat
    // à 1e-7 près, ce qui suffit à casser l'orthonormalité de la base.
    void updateFromOrbit() {
        const double t = theta, p = phi, r = distance;
        pos = target + glm::dvec3(
            r * std::cos(p) * std::sin(t),
            r * std::sin(p),
            r * std::cos(p) * std::cos(t)
        );
        m_fwd = glm::normalize(target - pos);
    }

    // Mode libre : recalcule la direction depuis (yaw, pitch).
    // Normalisation explicite : invRotationMatrix() prend la TRANSPOSÉE,
    // qui n'est l'inverse que d'une matrice orthonormée.
    void updateFreeOrientation() {
        const double y = yaw, p = pitch;
        m_fwd = glm::normalize(glm::dvec3(
            std::cos(p) * std::sin(y),
            std::sin(p),
            std::cos(p) * std::cos(y)
        ));
    }

    void update() {
        if (mode == Mode::Orbit) updateFromOrbit();
        else                     updateFreeOrientation();
    }

    // ═════════════════════════════════════════════════════════════════
    //  Base orthonormée
    // ═════════════════════════════════════════════════════════════════
    glm::dvec3 forward() const { return m_fwd; }

    glm::dvec3 right() const {
        glm::dvec3 wup(0, 1, 0);
        if (glm::abs(glm::dot(m_fwd, wup)) > 0.999) wup = glm::dvec3(0, 0, 1);
        return glm::normalize(glm::cross(m_fwd, wup));
    }

    glm::dvec3 up() const { return glm::cross(right(), m_fwd); }

    // Déplacement en vol libre, exprimé dans les axes de la caméra
    // (x = droite, y = haut, z = avant), en km.
    void moveFree(const glm::dvec3& d) {
        pos += right() * d.x + up() * d.y + m_fwd * d.z;
    }

    // ═════════════════════════════════════════════════════════════════
    //  Bascule entre les deux modes, en préservant EXACTEMENT la vue
    // ═════════════════════════════════════════════════════════════════

    // Orbite → Libre. La position ne bouge pas et la direction est
    // conservée : yaw = theta + pi, pitch = -phi (le vecteur orbital
    // pointe de la cible vers la caméra, donc à l'opposé du regard).
    void toFree() {
        yaw   = float(double(theta) + glm::pi<double>());
        pitch = -phi;
        mode  = Mode::Free;
        updateFreeOrientation();
    }

    // Libre → Orbite autour de `newTarget` (local au contexte courant).
    // On reconstruit (theta, phi, distance) depuis la position actuelle,
    // donc la caméra ne saute pas ; seule la direction se recale sur la
    // cible, ce qui la met pile au centre.
    void toOrbit(const glm::dvec3& newTarget) {
        target = newTarget;
        glm::dvec3 d = pos - target;
        double r = glm::length(d);
        if (r < 1e-9) { d = glm::dvec3(0, 0, 1); r = 1.0; }
        distance = float(r);
        phi      = float(std::asin(glm::clamp(d.y / r, -1.0, 1.0)));
        theta    = float(std::atan2(d.x, d.z));
        mode     = Mode::Orbit;
        updateFromOrbit();
    }

    // ═════════════════════════════════════════════════════════════════
    //  Matrice de ROTATION pure (float) — sans aucune translation.
    //  La translation est absorbée côté CPU en double.
    //  Inverse = transposée (matrice orthogonale).
    // ═════════════════════════════════════════════════════════════════
    glm::mat4 rotationMatrix() const {
        glm::dvec3 fwd = m_fwd;
        glm::dvec3 rgt = right();
        glm::dvec3 u   = glm::cross(rgt, fwd);

        // Column-major OpenGL : chaque groupe de 4 = une colonne
        return glm::mat4(
            float(rgt.x), float(u.x), float(-fwd.x), 0.f,
            float(rgt.y), float(u.y), float(-fwd.y), 0.f,
            float(rgt.z), float(u.z), float(-fwd.z), 0.f,
            0.f,          0.f,        0.f,           1.f
        );
    }

    glm::mat4 invRotationMatrix() const {
        return glm::transpose(rotationMatrix());
    }

private:
    glm::dvec3 m_fwd = { 0.0, 0.0, -1.0 };
};
