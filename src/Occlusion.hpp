#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Occlusion.hpp — Prédicats géométriques d'ombre portée (SOLID: SRP)
//
//  Le fragment shader ne connaît QUE les corps qu'on lui transmet : il y
//  cherche sa source de lumière et ses occulteurs. Un corps écarté par le
//  frustum culling cesse donc d'exister pour lui — et son ombre disparaît,
//  alors même que l'ombre, elle, est parfaitement dans le champ.
//
//  Il faut donc retenir aussi les corps qui, bien qu'invisibles, projettent
//  une ombre sur un corps visible. C'est de la géométrie pure : aucune
//  dépendance à OpenGL, donc testable directement.
// ════════════════════════════════════════════════════════════════════════

#include <glm/glm.hpp>

namespace Occlusion {

// Le corps B peut-il projeter une ombre sur le corps A, éclairé par `light` ?
//
// Test du cylindre : B intercepte de la lumière destinée à A si son centre
// passe à moins de (rA + rB) du SEGMENT qui relie A à la source. C'est
// conservateur — il suffit qu'il soit vrai chaque fois qu'une ombre est
// possible, quitte à retenir parfois un corps pour rien.
//
// Toutes les positions sont exprimées dans le même repère (ici, relatif à
// la caméra), en double.
inline bool castsShadow(const glm::dvec3& a, double rA,
                        const glm::dvec3& light,
                        const glm::dvec3& b, double rB)
{
    glm::dvec3 axis = light - a;
    double     len  = glm::length(axis);
    if (len < 1e-9) return false;
    glm::dvec3 dir = axis / len;

    // Projection de B sur l'axe A→lumière
    double t = glm::dot(b - a, dir);
    if (t <= 0.0 || t >= len) return false;      // B n'est pas entre les deux

    double d = glm::length(a + dir * t - b);     // écart à l'axe
    return d < rA + rB;
}

} // namespace Occlusion
