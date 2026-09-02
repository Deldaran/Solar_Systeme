#pragma once

// ════════════════════════════════════════════════════════════════════════
//  NoiseGLSL.hpp — Bibliothèque de bruit procédural GLSL (SOLID: SRP)
//
//  Le ray casting analytique donne, à chaque fragment, le point d'impact
//  et la normale EXACTS sur la sphère. On évalue donc le bruit directement
//  en espace objet 3D : pas de coordonnées UV, donc pas de couture ni de
//  distorsion aux pôles, et un détail qui ne s'épuise jamais au zoom.
//
//  Hachage entier plutôt que le classique fract(sin(...)*43758.5) : ce
//  dernier produit des bandes visibles sur certains GPU parce que la
//  précision de sin() aux grands arguments n'est pas garantie.
// ════════════════════════════════════════════════════════════════════════

namespace Shaders {

static const char* NOISE = R"GLSL(
// ── Hachage entier pcg3d (Jarzynski & Olano, « Hash Functions for GPU
//    Rendering »). Deux tours de mélange croisé + un décalage : chaque bit
//    d'entrée influence tous les bits de sortie.
//    Un hachage plus faible laisse les gradients corrélés le long des axes,
//    ce qui produit des escaliers bien visibles sur les côtes.
vec3 hash33(vec3 p) {
    uvec3 v = uvec3(ivec3(floor(p)));
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return vec3(v) * (1.0 / float(0xffffffffu)) * 2.0 - 1.0;
}

// Gradient de bruit : le vecteur DOIT être normalisé. Tiré composante par
// composante, il se répartit dans un CUBE et non sur une sphère : les
// diagonales sont alors racine(3) fois plus longues que les axes, ce qui
// aligne les iso-contours du bruit sur la grille et produit des terrasses
// rectangulaires bien visibles sur les côtes.
vec3 gradient33(vec3 p) {
    vec3 g = hash33(p);
    float l = length(g);
    return (l > 1e-6) ? g / l : vec3(0.57735027);
}

float hash11(float x) {
    uint n = floatBitsToUint(x);
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / float(0x7fffffff);
}

// ── Bruit de gradient 3D ────────────────────────────────────────────────
float gnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = p - i;
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);   // quintique (C2)

    return 1.4 * mix(
        mix(mix(dot(gradient33(i + vec3(0,0,0)), f - vec3(0,0,0)),
                dot(gradient33(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
            mix(dot(gradient33(i + vec3(0,1,0)), f - vec3(0,1,0)),
                dot(gradient33(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
        mix(mix(dot(gradient33(i + vec3(0,0,1)), f - vec3(0,0,1)),
                dot(gradient33(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
            mix(dot(gradient33(i + vec3(0,1,1)), f - vec3(0,1,1)),
                dot(gradient33(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z);
}

// ── fBm : somme d'octaves, chacune deux fois plus fine et deux fois
//    moins forte. Le nombre d'octaves est piloté par la taille apparente
//    du corps à l'écran (première brique du futur LOD).
float fbm(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum  += amp * gnoise(p);
        norm += amp;
        p    *= 2.02;      // lacunarité non entière : évite l'alignement
        amp  *= 0.5;
    }
    return sum / max(norm, 1e-5);          // ramené dans [-1, 1]
}

// ── Bruit « ridged » : crêtes nettes, pour montagnes, canyons, fractures
float ridged(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0, prev = 1.0;
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0 - abs(gnoise(p));
        n *= n;
        sum  += amp * n * prev;            // pondération par l'octave précédente
        prev  = n;                         // -> crêtes continues, pas de bouillie
        norm += amp;
        p    *= 2.02;
        amp  *= 0.5;
    }
    return sum / max(norm, 1e-5);          // [0, 1]
}

// ── Bruit « billow » : bosses arrondies (dunes, nuages)
float billow(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum  += amp * abs(gnoise(p));
        norm += amp;
        p    *= 2.02;
        amp  *= 0.5;
    }
    return sum / max(norm, 1e-5);          // [0, 1]
}

// ── Bruit cellulaire (Worley) ───────────────────────────────────────────
// Distance aux points d'une grille jitterée. F1 donne les cratères et les
// cellules de convection, F2-F1 les fractures et les joints de plaques.
// Aucun bruit à somme d'octaves ne sait produire ça.
vec2 worley(vec3 p) {
    vec3  i = floor(p);
    vec3  f = p - i;
    float f1 = 9.0, f2 = 9.0;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        vec3  g = vec3(float(x), float(y), float(z));
        vec3  o = hash33(i + g) * 0.5 + 0.5;       // point jitteré dans [0,1]^3
        float d = length(g + o - f);
        if (d < f1)      { f2 = f1; f1 = d; }
        else if (d < f2) { f2 = d; }
    }
    return vec2(f1, f2);
}

// ── Champ de cratères ───────────────────────────────────────────────────
// Profil réaliste : cuvette centrale + bourrelet surélevé. `density`
// choisit quelle proportion de cellules porte réellement un cratère, et
// les tailles varient d'une cellule à l'autre.
float craters(vec3 p, float density) {
    float d = worley(p).x;
    float r = 0.34;
    float bowl = smoothstep(r, r * 0.15, d);               // creux
    float rim  = smoothstep(r * 1.30, r, d) * smoothstep(r * 0.80, r, d);
    return (rim * 1.25 - bowl * 0.85) * density;
}

// ── Déformation du domaine : donne aux continents des côtes sinueuses
//    plutôt que les taches rondes d'un fBm brut.
vec3 warp(vec3 p, float strength, int octaves) {
    vec3 q = vec3(fbm(p + vec3(0.0, 0.0, 0.0), octaves),
                  fbm(p + vec3(5.2, 1.3, 2.7), octaves),
                  fbm(p + vec3(9.2, 7.3, 4.1), octaves));
    return p + strength * q;
}

// ── Rotation de teinte dans l'espace RGB (Rodrigues autour de la
//    diagonale des gris) : fait varier une famille de couleurs sans
//    toucher à sa luminosité.
vec3 hueShift(vec3 c, float angle) {
    const vec3 k = vec3(0.57735027);
    float cs = cos(angle), sn = sin(angle);
    return c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs);
}

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
)GLSL";

} // namespace Shaders
