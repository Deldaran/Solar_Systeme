#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Shaders.hpp — Sources GLSL (SOLID: SRP)
//
//  1) Passe « corps »  : ray casting analytique plein écran
//     - Caméra à l'origine (posRel = pos_monde - pos_caméra, en float)
//     - Fond étoilé procédural, Phong + ombres portées, limbe atmosphérique
//     - Assombrissement centre-bord solaire + couronne
//  2) Passe « trail »  : ligne d'orbite, occultée analytiquement par les
//     corps (le ray caster n'écrit pas de depth buffer, donc on teste
//     l'occlusion à la main — cohérent et exact).
// ════════════════════════════════════════════════════════════════════════

namespace Shaders {

// ════════════════════════════════════════════════════════════════════════
//  PASSE 1 — CORPS
// ════════════════════════════════════════════════════════════════════════

// Reconstruit la direction du rayon en espace monde à partir de la matrice
// de ROTATION PURE (sans translation — la translation est absorbée dans
// posRel côté CPU, en double).
static const char* VS = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;

uniform mat4  uInvRot;  // rotation inverse caméra (float, sans translation)
uniform float uHalfW;   // demi-largeur frustum (tan(fov/2) * aspect)
uniform float uHalfH;   // demi-hauteur frustum (tan(fov/2))

out vec3 vRayDir;

void main() {
    vec3 dirCam = vec3(aPos.x * uHalfW, aPos.y * uHalfH, -1.0);
    vRayDir = (uInvRot * vec4(dirCam, 0.0)).xyz;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* FS = R"GLSL(
#version 330 core

in  vec3 vRayDir;
out vec4 fragColor;

// ── Structure corps (positions relatives à la caméra) ────────────────────
struct SphereBody {
    vec3  posRel;    // position relative à la caméra (km, float)
    float radius;    // rayon visuel (km)
    vec3  color;
    float emissive;  // 1 = étoile, 0 = planète
};

uniform int        uBodyCount;
uniform SphereBody uBodies[32];
uniform float      uExposure;

// ── Utilitaires ───────────────────────────────────────────────────────────
float hash(vec3 p) {
    p = fract(p * vec3(443.8975, 397.2973, 491.1871));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

// Fond étoilé procédural 3 couches
vec3 starField(vec3 dir) {
    vec3 col = vec3(0.01, 0.02, 0.06);
    for (int i = 0; i < 3; i++) {
        float  scale = 80.0 + float(i) * 120.0;
        vec3   q     = floor(dir * scale);
        float  h     = hash(q);
        if (h > 0.997) {
            float bright = (h - 0.997) / 0.003;
            vec3  sc     = mix(vec3(0.9,0.95,1.0), vec3(1.0,0.9,0.7), hash(q * 7.3));
            col += sc * bright * 1.5;
        }
    }
    return col;
}

// Intersection analytique rayon/sphère (rd normalisé)
bool raySphere(vec3 ro, vec3 rd, vec3 ce, float ra, out float tNear, out float tFar) {
    vec3  oc = ro - ce;
    float b  = dot(oc, rd);
    float c  = dot(oc, oc) - ra * ra;
    float h  = b * b - c;
    if (h < 0.0) return false;
    h     = sqrt(h);
    tNear = -b - h;
    tFar  = -b + h;
    return tFar > 0.001;
}

// Shadow ray : 0 si occulté, 1 sinon
float hardShadow(vec3 ro, vec3 lightPos, int selfId) {
    vec3  rd   = normalize(lightPos - ro);
    float dMax = length(lightPos - ro);
    for (int i = 0; i < uBodyCount; i++) {
        if (i == selfId)               continue;
        if (uBodies[i].emissive > 0.5) continue;
        float t0, t1;
        if (raySphere(ro, rd, uBodies[i].posRel, uBodies[i].radius, t0, t1))
            if (t0 > 0.01 && t0 < dMax) return 0.0;
    }
    return 1.0;
}

// Éclairage Phong pour planète
vec3 shadePlanet(vec3 hitPt, vec3 N, vec3 albedo, int bodyId) {
    vec3  lightPos = vec3(0.0);
    bool  hasLight = false;
    for (int i = 0; i < uBodyCount; i++) {
        if (uBodies[i].emissive > 0.5) { lightPos = uBodies[i].posRel; hasLight = true; break; }
    }
    if (!hasLight) return albedo * 0.05;

    vec3  L = normalize(lightPos - hitPt);
    vec3  V = normalize(-hitPt);              // caméra à l'origine
    vec3  H = normalize(L + V);

    float diff = max(0.0, dot(N, L));
    float spec = pow(max(0.0, dot(N, H)), 64.0) * 0.4;
    // Biais d'ombre proportionnel au rayon : robuste à toutes les échelles
    float bias = max(1.0, uBodies[bodyId].radius * 1e-4);
    float shad = hardShadow(hitPt + N * bias, lightPos, bodyId);

    // Liseré atmosphérique (limbe), visible surtout côté éclairé
    float rim  = 1.0 - max(0.0, dot(N, V));
    vec3  atmo = albedo * pow(rim, 4.0) * 0.5 * max(0.0, dot(N, L) + 0.3);

    float amb  = 0.025;
    return albedo * (amb + shad * diff * 0.975) + vec3(shad * spec) + atmo;
}

// Surface stellaire : granulation + assombrissement centre-bord.
//
// Loi classique : I(mu)/I(0) = 1 - u*(1 - mu), avec mu = cos de l'angle
// entre la normale et la direction d'observation. u ~ 0.6 pour le Soleil.
// (L'ancienne version calculait dot(N, -normalize(N)) == -1, donc l'effet
//  était constant et ne faisait rien.)
vec3 shadeStar(vec3 hitPt, vec3 N, vec3 baseColor) {
    vec3  V     = normalize(-hitPt);
    float mu    = clamp(dot(N, V), 0.0, 1.0);
    float limb  = 1.0 - 0.6 * (1.0 - mu);

    float grain = hash(floor(N * 18.0)) * 0.15 - 0.075;
    vec3  hot   = mix(baseColor, vec3(1.0, 0.98, 0.8), 0.4 + grain);
    return hot * limb * 1.6;
}

// Couronne / halo
vec3 solarGlow(vec3 rd) {
    vec3 glow = vec3(0.0);
    for (int i = 0; i < uBodyCount; i++) {
        if (uBodies[i].emissive < 0.5) continue;
        vec3  toSun = uBodies[i].posRel;
        float dist  = length(toSun);
        if (dist < 1e-6) continue;
        vec3  dirS  = toSun / dist;
        float cosA  = clamp(dot(rd, dirS), -1.0, 1.0);
        float angR  = atan(uBodies[i].radius / dist);
        float angC  = acos(cosA);
        float g     = smoothstep(angR * 10.0, angR * 0.8, angC);
        glow += uBodies[i].color * g * 0.55;
    }
    return glow;
}

void main() {
    vec3 ro = vec3(0.0);       // caméra à l'origine
    vec3 rd = normalize(vRayDir);

    float tBest  = 1e30;
    int   idBest = -1;

    for (int i = 0; i < uBodyCount; i++) {
        float t0, t1;
        if (raySphere(ro, rd, uBodies[i].posRel, uBodies[i].radius, t0, t1)) {
            float t = (t0 > 0.001) ? t0 : t1;
            if (t > 0.001 && t < tBest) { tBest = t; idBest = i; }
        }
    }

    vec3 col;
    if (idBest < 0) {
        col  = starField(rd);
        col += solarGlow(rd);
    } else {
        vec3 hitPt = ro + rd * tBest;
        vec3 N     = normalize(hitPt - uBodies[idBest].posRel);
        if (uBodies[idBest].emissive > 0.5)
            col = shadeStar(hitPt, N, uBodies[idBest].color);
        else
            col = shadePlanet(hitPt, N, uBodies[idBest].color, idBest);
    }

    // Tone mapping Reinhard + gamma
    col *= uExposure;
    col  = col / (col + vec3(1.0));
    col  = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
)GLSL";

// ════════════════════════════════════════════════════════════════════════
//  PASSE 2 — TRAIL D'ORBITE
// ════════════════════════════════════════════════════════════════════════

// Projection sans near/far : on pose clip = (x, y, 0, -z_cam).
// Le w négatif derrière la caméra assure un clipping correct des segments,
// et z=0 place la ligne toujours dans [-w,w] — inutile de choisir des plans
// near/far, ce qui serait un cauchemar à des échelles de 10^9 km.
static const char* TRAIL_VS = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPosRel;   // position relative à la caméra (km)

uniform mat4  uRot;      // rotation monde → caméra
uniform float uHalfW;
uniform float uHalfH;
uniform int   uCount;    // nombre de points du trail

out vec3  vPosRel;
out float vFade;         // 0 = plus ancien, 1 = plus récent

void main() {
    vec3 pc = (uRot * vec4(aPosRel, 0.0)).xyz;
    vPosRel = aPosRel;
    vFade   = (uCount > 1) ? float(gl_VertexID) / float(uCount - 1) : 1.0;
    gl_Position = vec4(pc.x / uHalfW, pc.y / uHalfH, 0.0, -pc.z);
}
)GLSL";

static const char* TRAIL_FS = R"GLSL(
#version 330 core

in  vec3  vPosRel;
in  float vFade;
out vec4  fragColor;

struct SphereBody {
    vec3  posRel;
    float radius;
    vec3  color;
    float emissive;
};

uniform int        uBodyCount;
uniform SphereBody uBodies[32];
uniform vec3       uTrailColor;

bool raySphere(vec3 ro, vec3 rd, vec3 ce, float ra, out float tNear, out float tFar) {
    vec3  oc = ro - ce;
    float b  = dot(oc, rd);
    float c  = dot(oc, oc) - ra * ra;
    float h  = b * b - c;
    if (h < 0.0) return false;
    h     = sqrt(h);
    tNear = -b - h;
    tFar  = -b + h;
    return tFar > 0.001;
}

void main() {
    // Occlusion : le ray caster n'écrit pas de depth, donc on teste si un
    // corps s'interpose entre la caméra (origine) et ce point du trail.
    float dist = length(vPosRel);
    vec3  rd   = vPosRel / dist;
    for (int i = 0; i < uBodyCount; i++) {
        float t0, t1;
        if (raySphere(vec3(0.0), rd, uBodies[i].posRel, uBodies[i].radius, t0, t1))
            if (t0 > 0.001 && t0 < dist) discard;
    }
    fragColor = vec4(uTrailColor, 0.15 + 0.55 * vFade);
}
)GLSL";

} // namespace Shaders
