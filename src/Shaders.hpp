#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Shaders.hpp — Assemblage des sources GLSL (SOLID: SRP)
//
//  Le fragment shader des corps est monté à partir de trois briques :
//     FS_HEAD    déclarations, uniformes, intersection rayon/sphère
//     NOISE      bibliothèque de bruit          (NoiseGLSL.hpp)
//     SURFACE    les quatre catégories de monde (SurfaceGLSL.hpp)
//     FS_BODY    éclairage, ombres, main()
//  Chaque brique reste lisible et testable séparément.
// ════════════════════════════════════════════════════════════════════════

#include "NoiseGLSL.hpp"
#include "SurfaceGLSL.hpp"
#include "CloudsGLSL.hpp"
#include <string>

namespace Shaders {

// ════════════════════════════════════════════════════════════════════════
//  PASSE 1 — CORPS
// ════════════════════════════════════════════════════════════════════════

// Reconstruit la direction du rayon en espace monde à partir de la matrice
// de ROTATION PURE (la translation est absorbée côté CPU, en double).
static const char* VS = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;

uniform mat4  uInvRot;
uniform float uHalfW;
uniform float uHalfH;

out vec3 vRayDir;

void main() {
    vec3 dirCam = vec3(aPos.x * uHalfW, aPos.y * uHalfH, -1.0);
    vRayDir = (uInvRot * vec4(dirCam, 0.0)).xyz;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";


static const char* CUBE = R"GLSL(
// ── Correspondance direction <-> (face, uv) d'un cube ───────────────────
// On stocke les surfaces sur les 6 faces d'un cube plutôt que sur une
// carte équirectangulaire : densité de texels uniforme, et surtout aucune
// singularité aux pôles — une équirectangulaire y écrase toute une ligne
// de texels sur un point.
// uCubeK > 1 : chaque face est cuite sur un angle LEGEREMENT plus large que
// 90 degres. L'echantillonnage ramene ensuite les +-1 reels dans la partie
// interne de la texture, si bien que le filtrage lineaire du bord dispose
// de vrais texels voisins au lieu de repeter le dernier -- sans quoi une
// couture fine apparait le long de chaque arete du cube.
vec3 dirFromFace(int face, vec2 uv) {
    vec2 t = (uv * 2.0 - 1.0) * uCubeK;
    if (face == 0) return normalize(vec3( 1.0, -t.y, -t.x));
    if (face == 1) return normalize(vec3(-1.0, -t.y,  t.x));
    if (face == 2) return normalize(vec3( t.x,  1.0,  t.y));
    if (face == 3) return normalize(vec3( t.x, -1.0, -t.y));
    if (face == 4) return normalize(vec3( t.x, -t.y,  1.0));
    return             normalize(vec3(-t.x, -t.y, -1.0));
}

void faceFromDir(vec3 d, out float face, out vec2 uv) {
    vec3  a = abs(d);
    float ma; vec2 st;
    if (a.x >= a.y && a.x >= a.z) {
        ma = a.x;
        if (d.x > 0.0) { face = 0.0; st = vec2(-d.z, -d.y); }
        else           { face = 1.0; st = vec2( d.z, -d.y); }
    } else if (a.y >= a.z) {
        ma = a.y;
        if (d.y > 0.0) { face = 2.0; st = vec2(d.x,  d.z); }
        else           { face = 3.0; st = vec2(d.x, -d.z); }
    } else {
        ma = a.z;
        if (d.z > 0.0) { face = 4.0; st = vec2( d.x, -d.y); }
        else           { face = 5.0; st = vec2(-d.x, -d.y); }
    }
    uv = st / max(ma, 1e-9) / uCubeK * 0.5 + 0.5;
}
)GLSL";

static const char* FS_HEAD = R"GLSL(
#version 330 core

in  vec3 vRayDir;
out vec4 fragColor;

struct SphereBody {
    vec3  posRel;     // position relative à la caméra (km, float)
    float radius;     // rayon visuel (km)
    vec3  color;      // teinte identitaire du corps
    float emissive;   // 1 = étoile, 0 = planète
    float surfType;   // 0 tellurique, 1 désertique, 2 glacée, 3 gazeuse
    float surfSeed;   // graine de variation
    float slot;       // emplacement dans le cache de surfaces (-1 = aucun)
};

uniform int        uBodyCount;
uniform SphereBody uBodies[32];
uniform float      uExposure;
uniform float      uHalfH;
uniform float      uScreenH;    // hauteur du framebuffer, en pixels

// ── Cache de surfaces ────────────────────────────────────────────────────
// 6 faces de cube par corps, empilées dans un tableau de textures 2D.
// Remplace ~400 évaluations de hachage par fragment par une lecture.
uniform sampler2DArray uCache;
uniform float          uUseCache;
uniform float          uCubeK;       // debord des faces de cube
uniform float          uCloudBase;   // 1ere couche de nuages (< 0 = aucune)
uniform float          uTime;        // temps simule, fait tourner la meteo
uniform vec3           uLightTint;   // couleur de l'etoile eclairante

// Intersection analytique rayon/sphère (rd normalisé).
//
// La forme scolaire calcule h = b*b - (dot(oc,oc) - ra*ra). Pour un corps
// à 10^9 km, ces deux termes valent ~10^18 alors que ra*ra ne vaut que
// ~10^9 : en float32 leur ulp est de 10^11, et la soustraction ne rend que
// du bruit. Le test d'intersection devient alors aléatoire — d'où des
// bandes d'ombre parasites sur les planètes.
//
// On passe par la composante PERPENDICULAIRE : oc = b*rd + d avec d ⊥ rd,
// donc |oc|² = b² + |d|² et h = ra² - |d|². Les deux termes restent
// petits, et plus aucune grande quantité ne s'annule.
bool raySphere(vec3 ro, vec3 rd, vec3 ce, float ra, out float tNear, out float tFar) {
    vec3  oc = ro - ce;
    float b  = dot(oc, rd);
    vec3  d  = oc - b * rd;
    float h  = ra * ra - dot(d, d);
    if (h < 0.0) return false;
    h     = sqrt(h);
    tNear = -b - h;
    tFar  = -b + h;
    return tFar > 0.001;
}
)GLSL";

static const char* FS_BODY = R"GLSL(
// ── Niveau de détail ────────────────────────────────────────────────────
// Le coût du bruit est linéaire en nombre d'octaves, et une octave plus
// fine que le pixel ne fait qu'ajouter du scintillement. On indexe donc
// le nombre d'octaves sur la taille apparente du corps à l'écran : une
// octave de plus à chaque doublement. C'est la première brique du LOD.
// Pas de marche des nuages : meme principe que les octaves, indexe sur la
// taille apparente. Une planete de quelques pixels n'a pas besoin de 32
// pas pour montrer sa couche nuageuse.
int cloudStepsFor(float radius, float dist) {
    float px = (radius / max(dist, 1e-6)) / (2.0 * uHalfH) * uScreenH;
    return clamp(int(px * 0.06), 6, 40);
}

int octavesFor(float radius, float dist) {
    float px = (radius / max(dist, 1e-6)) / (2.0 * uHalfH) * uScreenH;
    return clamp(int(log2(max(px, 2.0))) + 1, 3, 10);
}

// Fond étoilé procédural, 3 couches.
// Chaque cellule de la grille peut contenir UNE étoile, placée à une
// position aléatoire dans la cellule et rendue comme un point rond. La
// version naïve — colorer la cellule entière — donnait des carrés.
vec3 starField(vec3 dir) {
    vec3 col = vec3(0.008, 0.014, 0.042);
    for (int i = 0; i < 3; i++) {
        float scale = 140.0 + float(i) * 210.0;
        vec3  p     = dir * scale;
        vec3  cell  = floor(p);
        vec3  f     = p - cell;

        vec3 h = hash33(cell) * 0.5 + 0.5;          // [0,1]
        if (h.z > 0.982) {
            vec3  centre = vec3(h.x, h.y, hash33(cell * 1.7).x * 0.5 + 0.5);
            float d      = length(f - centre);
            float bright = (h.z - 0.982) / 0.018;
            float dot_   = smoothstep(0.085, 0.0, d);      // point rond
            vec3  sc     = mix(vec3(0.82, 0.90, 1.0),      // bleue
                               vec3(1.0,  0.86, 0.68),     // orangée
                               h.x);
            col += sc * dot_ * bright * bright * 2.4;
        }
    }
    return col;
}

// Ombre portée : 0 si occulté, 1 sinon
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

// ── Relief : perturbation de la normale ─────────────────────────────────
// La sphère est analytique, donc géométriquement lisse. Pour qu'on voie
// des montagnes il faut perturber la NORMALE d'après le gradient du champ
// d'altitude — on l'estime par différences finies le long de deux
// tangentes. Les géantes gazeuses n'ont pas de surface solide : on les
// laisse lisses.
vec3 bumpNormal(vec3 N, float seed, int type, int oct) {
    if (type == 3) return N;

    // 3 evaluations de fBm : on plafonne les octaves, le relief fin est
    // de toute façon la seule composante visible en gros plan.
    int   o   = clamp(oct - 2, 3, 6);
    float eps = 0.010;
    vec3  p   = N * 2.6 + seedOffset(seed);

    // Base tangente stable (évite la singularité au pôle)
    vec3 up = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t1 = normalize(cross(N, up));
    vec3 t2 = cross(N, t1);

    float h0 = fbm(p, o);
    float h1 = fbm(p + t1 * eps * 2.6, o);
    float h2 = fbm(p + t2 * eps * 2.6, o);

    // Amplitude croissante avec le détail : de loin le relief est invisible
    float k = (type == 2) ? 0.55 : 1.0;          // la glace est plus lisse
    vec3  g = ((h1 - h0) * t1 + (h2 - h0) * t2) / eps;
    return normalize(N - g * 0.035 * k);
}

// ── Éclairage d'une planète, albédo procédural ──────────────────────────
vec3 shadePlanet(vec3 hitPt, vec3 N, int id, int oct) {
    vec3 lightPos = vec3(0.0);
    bool hasLight = false;
    for (int i = 0; i < uBodyCount; i++) {
        if (uBodies[i].emissive > 0.5) { lightPos = uBodies[i].posRel; hasLight = true; break; }
    }

    int   type = int(uBodies[id].surfType);
    float gloss;
    vec3  albedo;

    if (uUseCache > 0.5 && uBodies[id].slot >= 0.0) {
        // Lecture du cache : toute la structure basse et moyenne fréquence
        // est précalculée. Une seule lecture de texture au lieu du bruit.
        float face; vec2 uv;
        faceFromDir(N, face, uv);
        vec4 s = texture(uCache, vec3(uv, uBodies[id].slot * 6.0 + face));
        albedo = s.rgb;
        gloss  = s.a;

        // Détail fin AJOUTÉ seulement quand le corps est gros à l'écran.
        // Ces fréquences sont au-dessus de ce que la carte peut résoudre :
        // il n'y a donc pas de double comptage.
        // Détail fin AJOUTÉ uniquement quand le corps est gros à l'écran,
        // et sur des fréquences que la carte ne résout pas — sinon on
        // recompte ce qu'elle contient déjà, et on alias par-dessus.
        if (oct >= 9) {
            float d = fbm(N * 160.0 + seedOffset(uBodies[id].surfSeed), 2);
            albedo *= 1.0 + d * 0.10;
        }
    } else {
        albedo = surfaceColor(type, uBodies[id].surfSeed,
                              uBodies[id].color, N, oct, gloss);
    }
    if (!hasLight) return albedo * 0.05;

    // Relief : la normale de l'éclairage est perturbée, pas la géométrie.
    // On garde N pour le limbe et l'atmosphère, qui suivent la sphère.
    vec3 Nl = mix(bumpNormal(N, uBodies[id].surfSeed, type, oct), N,
                  gloss);           // l'eau et la glace restent lisses

    vec3  L = normalize(lightPos - hitPt);
    vec3  V = normalize(-hitPt);
    vec3  H = normalize(L + V);

    float diff = max(0.0, dot(Nl, L));
    // Le brillant dépend du matériau : l'eau et la glace réfléchissent,
    // la roche et le sable non.
    float spec = pow(max(0.0, dot(Nl, H)), mix(16.0, 220.0, gloss)) * gloss;

    float bias = max(1.0, uBodies[id].radius * 1e-4);
    float shad = hardShadow(hitPt + N * bias, lightPos, id);

    // Liseré atmosphérique, côté éclairé
    float rim   = 1.0 - max(0.0, dot(N, V));
    float atmoK = (type == 3) ? 0.9 : 0.5;                    // gazeuse = plus épais
    vec3  atmo  = albedo * pow(rim, 4.0) * atmoK * max(0.0, dot(N, L) + 0.3);

    float amb = 0.025;
    return albedo * (amb + shad * diff * 0.975) + vec3(shad * spec) + atmo;
}

// ── Photosphère stellaire ───────────────────────────────────────────────
// Une étoile n'est PAS une surface éclairée : elle émet. Rendue à une
// luminance de l'ordre de l'unité, elle ressort comme un caillou beige.
// Il faut émettre très au-dessus de 1 pour que le tone mapping Reinhard
// la sature vers le blanc — c'est ce qui la fait lire comme une source.
vec3 shadeStar(vec3 hitPt, vec3 N, vec3 baseColor, float seed, int oct) {
    vec3  V  = normalize(-hitPt);
    float mu = clamp(dot(N, V), 0.0, 1.0);

    // Assombrissement centre-bord d'Eddington : I(mu)/I(0) = 0.4 + 0.6 mu
    float limb = 0.4 + 0.6 * mu;

    vec3 p = N * 9.0 + seedOffset(seed);

    // Granulation convective : Worley donne de vraies cellules jointives,
    // ce que la structure réelle de la photosphère est. Contraste faible.
    vec2  gw   = worley(p * 3.0);
    float gran = smoothstep(0.0, 0.22, gw.y - gw.x);        // joints sombres
    float fine = fbm(p * 7.0, min(oct, 5)) * 0.5 + 0.5;

    // Taches : rares, sombres, avec pénombre — pas un réseau de veines.
    float spotField = fbm(p * 0.65 + 55.0, 4) * 0.5 + 0.5;
    float umbra     = smoothstep(0.74, 0.86, spotField);

    vec3 photo = mix(baseColor, vec3(1.0, 0.97, 0.90), 0.55);
    photo *= mix(0.93, 1.06, gran * 0.55 + fine * 0.45);  // granulation discrète
    photo  = mix(photo, baseColor * 0.10, umbra * 0.9);   // ombre des taches

    return photo * limb * 5.0;      // >> 1 : sature vers le blanc incandescent
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
        float angR  = atan(uBodies[i].radius / dist);      // rayon apparent
        float angC  = acos(cosA);                          // écart au centre

        // Brillance coronale : elle chute comme une puissance de la
        // distance au limbe, exprimée en RAYONS STELLAIRES. Une extension
        // proportionnelle au rayon apparent (« angR * 10 ») valait
        // plusieurs radians vue de près et badigeonnait tout le ciel.
        float x = angC / max(angR, 1e-9);      // 1 = limbe, 2 = un rayon plus loin
        float g = (x > 1.0) ? pow(1.0 / x, 3.5) : 1.0;
        glow += uBodies[i].color * g * 0.40;
    }
    return glow;
}

void main() {
    vec3 ro = vec3(0.0);          // caméra à l'origine
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
        int  oct   = octavesFor(uBodies[idBest].radius,
                                length(uBodies[idBest].posRel));

        if (uBodies[idBest].emissive > 0.5)
            col = shadeStar(hitPt, N, uBodies[idBest].color,
                            uBodies[idBest].surfSeed, oct);
        else
            col = shadePlanet(hitPt, N, idBest, oct);
    }

    // ── Nuages ───────────────────────────────────────────────────────
    // On cherche la coquille traversee la plus proche : elle peut
    // appartenir a un corps qu'on ne touche pas (nuages vus au limbe).
    if (uCloudBase >= 0.0) {
        int   idCloud = -1;
        float bestEntry = 1e30;
        for (int i = 0; i < uBodyCount; i++) {
            if (uBodies[i].emissive > 0.5 || uBodies[i].slot < 0.0) continue;
            if (uBodies[i].surfType > 2.5) continue;      // geante gazeuse
            float c0, c1;
            if (!raySphere(ro, rd, uBodies[i].posRel,
                           uBodies[i].radius * CLOUD_TOP, c0, c1)) continue;
            float entry = max(c0, 0.0);
            if (entry < tBest && entry < bestEntry) { bestEntry = entry; idCloud = i; }
        }
        if (idCloud >= 0) {
            vec3 lightPos = vec3(0.0);
            for (int i = 0; i < uBodyCount; i++)
                if (uBodies[i].emissive > 0.5) { lightPos = uBodies[i].posRel; break; }

            int steps = cloudStepsFor(uBodies[idCloud].radius,
                                      length(uBodies[idCloud].posRel));
            vec4 cl = marchClouds(ro, rd, idCloud, tBest, lightPos, steps);
            col = cl.rgb + col * cl.a;      // les nuages sont DEVANT la surface
        }
    }

    // ── Tone mapping sur la LUMINANCE ────────────────────────────────
    // Un Reinhard par canal comprime davantage le canal fort que le canal
    // faible : toute couleur saturée est ramenée vers le gris. Le rouille
    // de Mars (1.28, 0.63, 0.36) en ressortait beige. On compresse donc la
    // luminance et on conserve le rapport des canaux ; seules les très
    // hautes lumières sont poussées vers le blanc, comme une vraie
    // surexposition (c'est ce qui garde le Soleil blanc).
    col *= uExposure;
    float l  = max(luminance(col), 1e-6);
    float ln = l / (1.0 + l);
    col *= ln / l;
    col  = mix(col, vec3(ln), pow(ln, 4.0));
    col  = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
)GLSL";

// Montage final du fragment shader des corps.
inline std::string fragmentSource() {
    return std::string(FS_HEAD) + NOISE + CUBE + SURFACE
         + CLOUDS_MAP + CLOUDS_MARCH + FS_BODY;
}

// ════════════════════════════════════════════════════════════════════════
//  CUISSON DU CACHE DE SURFACES
// ════════════════════════════════════════════════════════════════════════
static const char* BAKE_VS = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* BAKE_HEAD = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;

uniform int   uFace;
uniform float uSurfType;
uniform float uSurfSeed;
uniform vec3  uTint;
uniform int   uOctaves;
uniform int   uMode;      // 0 = surface, 1 = carte de nuages
uniform float uCubeK;     // debord des faces de cube
)GLSL";

static const char* BAKE_MAIN = R"GLSL(
void main() {
    vec3 n = dirFromFace(uFace, vUV);
    if (uMode == 1) {
        // RGB A = couverture, erosion, altitude du sommet, orage
        fragColor = cloudMap(n, uSurfSeed, int(uSurfType), uOctaves);
    } else {
        float gloss;
        vec3  c = surfaceColor(int(uSurfType), uSurfSeed, uTint, n, uOctaves, gloss);
        // RGB = albedo, A = brillance du materiau (eau, glace)
        fragColor = vec4(c, gloss);
    }
}
)GLSL";

inline std::string bakeFragmentSource() {
    return std::string(BAKE_HEAD) + NOISE + CUBE + SURFACE + CLOUDS_MAP + BAKE_MAIN;
}


// ════════════════════════════════════════════════════════════════════════
//  PASSE 2 — TRAIL D'ORBITE
// ════════════════════════════════════════════════════════════════════════

// Projection sans near/far : clip = (x, y, 0, -z_cam). Le w négatif
// derrière la caméra assure un clipping correct des segments, et z = 0
// place la ligne toujours dans [-w, w] — inutile de choisir des plans
// near/far, ce qui serait un cauchemar à des échelles de 10^9 km.
static const char* TRAIL_VS = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPosRel;

uniform mat4  uRot;
uniform float uHalfW;
uniform float uHalfH;
uniform int   uCount;

out vec3  vPosRel;
out float vFade;

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
    float surfType;
    float surfSeed;
    float slot;
};

uniform int        uBodyCount;
uniform SphereBody uBodies[32];
uniform vec3       uTrailColor;

// Forme stable : voir la note du shader principal.
bool raySphere(vec3 ro, vec3 rd, vec3 ce, float ra, out float tNear, out float tFar) {
    vec3  oc = ro - ce;
    float b  = dot(oc, rd);
    vec3  d  = oc - b * rd;
    float h  = ra * ra - dot(d, d);
    if (h < 0.0) return false;
    h     = sqrt(h);
    tNear = -b - h;
    tFar  = -b + h;
    return tFar > 0.001;
}

void main() {
    // Le ray caster n'écrit pas de depth buffer : on teste donc à la main
    // si un corps s'interpose entre la caméra (origine) et ce point.
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
