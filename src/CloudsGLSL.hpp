#pragma once

// ════════════════════════════════════════════════════════════════════════
//  CloudsGLSL.hpp — Nuages volumétriques (SOLID: SRP)
//
//  Une coquille d'atmosphère raymarchée au-dessus de la surface.
//
//  ── Pourquoi une carte cuite plutôt que du bruit 3D pur ──────────────
//  Marcher du bruit volumétrique coûte, par fragment, une évaluation de
//  densité par pas de marche PLUS une par pas de la marche vers la
//  lumière : à 16 pas et 3 pas d'ombrage, cela fait 64 évaluations, soit
//  ~3000 hachages — huit fois le coût de toute l'ancienne surface.
//
//  On sépare donc ce qui varie en 2D de ce qui varie en 3D. La couverture,
//  l'érosion et l'altitude du sommet ne dépendent que de la DIRECTION :
//  elles sont cuites une fois dans la même texture de cube que les
//  surfaces. Le profil vertical, lui, est analytique. Une évaluation de
//  densité devient une lecture de texture et une poignée d'opérations.
//
//  C'est aussi la structure d'un vrai modèle météo : une carte de
//  couverture, un profil vertical, et du détail local.
// ════════════════════════════════════════════════════════════════════════

namespace Shaders {

// ── Génération de la carte (utilisée à la cuisson ET nulle part ailleurs)
static const char* CLOUDS_MAP = R"GLSL(
// La coquille nuageuse, en fraction du rayon du corps.
const float CLOUD_BASE = 1.0015;
const float CLOUD_TOP  = 1.0240;

// Couverture nuageuse d'une direction.
//   R = couverture      [0,1]
//   G = érosion         (bords déchiquetés)
//   B = altitude du sommet (cumulus bas / cirrus hauts)
//   A = intensité d'orage
//
// Les ceintures suivent la circulation atmosphérique réelle : convergence
// humide à l'équateur, ceintures sèches vers 30° là où redescendent les
// cellules de Hadley, fronts des moyennes latitudes vers 55°.
vec4 cloudMap(vec3 n, float seed, int type, int oct)
{
    // Pas de nuages sur un monde sans atmosphère
    float gAmount = geneR(seed, 61.0, 0.0, 1.0);
    if (type == 1) gAmount *= 0.25;         // désertique : voiles ténus
    if (type == 2) gAmount *= 0.45;         // glacée : brumes
    if (type == 3) return vec4(0.0);        // gazeuse : elle EST ses nuages

    float gBands = geneR(seed, 62.0, 0.35, 1.0);   // netteté des ceintures
    float gScale = geneR(seed, 63.0, 2.0,  6.0);   // taille des systèmes

    float lat = n.y;
    float itcz  = exp(-pow(lat / 0.14, 2.0)) * 0.60;
    float dry   = exp(-pow((abs(lat) - 0.47) / 0.17, 2.0)) * 0.55;
    float front = exp(-pow((abs(lat) - 0.80) / 0.20, 2.0)) * 0.40;
    // Un peu moins de bandes, un peu plus de chaos : la Terre reelle
    // a des ceintures marquees mais pas des rayures.
    float bands = (itcz - dry + front) * gBands * 0.75;

    vec3 p = n * gScale + seedOffset(seed) + 51.3;

    // Domaine déformé : donne aux systèmes des formes spiralées plutôt que
    // des taches rondes.
    vec3  q     = warp(p, 0.45, max(3, oct - 3));
    float large = fbm(q, oct) * 0.5 + 0.5;

    // Seuil haut : l'essentiel du globe doit rester dégagé, les nuages
    // se concentrant dans les ceintures. Un seuil trop bas noie la planète
    // sous une couche uniforme et on ne voit plus la surface.
    float cov = clamp(large * 1.35 - 0.63 + bands, 0.0, 1.0) * gAmount;

    float ero  = billow(p * 5.5 + 12.9, max(3, oct - 2));
    float top  = fbm(p * 3.1 + 88.0, max(3, oct - 3)) * 0.5 + 0.5;

    // Orages : rares, dans les zones de forte couverture
    float storm = smoothstep(0.72, 0.95, cov)
                * smoothstep(0.55, 0.85, billow(p * 2.2 + 140.0, 3));

    return vec4(cov, ero, top, storm);
}
)GLSL";

// ── Échantillonnage et raymarching (shader principal uniquement) ────────
static const char* CLOUDS_MARCH = R"GLSL(
// Rotation autour de l'axe Y : fait tourner la météo avec le temps simulé.
vec3 spinY(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

// Lecture de la carte de nuages du corps `id`, dans la direction `n`.
vec4 sampleCloudMap(int id, vec3 n) {
    float face; vec2 uv;
    faceFromDir(n, face, uv);
    return texture(uCache, vec3(uv, uCloudBase + uBodies[id].slot * 6.0 + face));
}

// Densité en un point de la coquille (espace objet, centré sur le corps).
float cloudDensity(int id, vec3 pObj, float R, float spin) {
    float r = length(pObj);
    float h = (r / R - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE);
    if (h < 0.0 || h > 1.0) return 0.0;

    vec3 n = spinY(pObj / r, spin);
    vec4 m = sampleCloudMap(id, n);

    float cov = m.r;
    if (cov < 0.02) return 0.0;

    // Profil vertical : base plate, sommet effiloché. L'altitude du sommet
    // varie d'un système à l'autre (cumulus bas, cirrus hauts).
    float top  = mix(0.45, 1.0, m.b);
    float vert = smoothstep(0.0, 0.16, h) * smoothstep(top, top * 0.55, h);

    float d = cov * vert;
    // Érosion : creuse les bords, davantage en altitude
    d -= m.g * 0.45 * (0.35 + 0.65 * h);
    return max(0.0, d) * 1.7;
}

// Fonction de phase de Henyey-Greenstein : concentre la lumière vers
// l'avant, ce qui donne le liseré argenté des nuages à contre-jour.
float phaseHG(float cosT, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (12.566371 * pow(1.0 + g2 - 2.0 * g * cosT, 1.5));
}

// Traversée de la coquille. Rend (lumière diffusée, transmittance).
vec4 marchClouds(vec3 ro, vec3 rd, int id, float tEnd, vec3 lightPos, int steps)
{
    if (uBodies[id].surfType > 2.5) return vec4(0.0, 0.0, 0.0, 1.0);

    vec3  ce = uBodies[id].posRel;
    float R  = uBodies[id].radius;

    float o0, o1;
    if (!raySphere(ro, rd, ce, R * CLOUD_TOP, o0, o1)) return vec4(0.0, 0.0, 0.0, 1.0);

    float tA = max(o0, 0.0);
    float tB = min(o1, tEnd);
    if (tB <= tA) return vec4(0.0, 0.0, 0.0, 1.0);

    // Une traversée rasante du limbe est bien plus longue qu'une traversée
    // verticale : on borne pour ne pas gaspiller la moitié des pas dans le
    // vide sous la couche.
    float maxLen = R * (CLOUD_TOP - 1.0) * 14.0;
    tB = min(tB, tA + maxLen);

    float spin  = uTime * 2.0e-7;
    vec3  L     = normalize(lightPos - ce);
    float cosT  = dot(rd, L);
    float phase = mix(phaseHG(cosT, 0.72), phaseHG(cosT, -0.25), 0.35) * 12.566371;

    float dt    = (tB - tA) / float(steps);
    float sigma = 1.0 / (R * 0.0042);        // extinction par km
    float lstep = R * 0.006;

    vec3  scatter = vec3(0.0);
    float trans   = 1.0;

    for (int i = 0; i < steps; ++i) {
        if (trans < 0.02) break;             // saturé : inutile de continuer
        float t = tA + (float(i) + 0.5) * dt;
        vec3  p = ro + rd * t - ce;

        float d = cloudDensity(id, p, R, spin);
        if (d > 0.002) {
            // Visibilité du Soleil : le corps lui-même occulte sa couche
            // nuageuse du côté nuit. Sans ce terme, la planète garde des
            // nuages éclatants sur sa face sombre.
            float sun = smoothstep(-0.03, 0.10, dot(normalize(p), L));

            // Ombrage : deux sondes vers la lumière suffisent sur une
            // couche aussi mince ; une marche complète coûterait le double
            // du reste du shader pour un gain invisible.
            float tau = cloudDensity(id, p + L * lstep,       R, spin) * lstep
                      + cloudDensity(id, p + L * lstep * 3.0, R, spin) * lstep * 2.0;
            float lightE = exp(-tau * sigma * 0.75) * sun;

            // Diffusion multiple approchée : les nuages épais restent
            // lumineux au lieu de virer au noir.
            float ms = (0.28 + 0.72 * lightE) * sun;

            float dTrans = exp(-d * dt * sigma);
            // Teinte de l'étoile, et un fond bleuté d'atmosphère diffuse
            vec3  lit    = uLightTint * (lightE * phase + ms * 0.30)
                         + vec3(0.05, 0.08, 0.13) * ms * 0.25;
            scatter += lit * (1.0 - dTrans) * trans;
            trans   *= dTrans;
        }
    }
    return vec4(scatter, trans);
}
)GLSL";

} // namespace Shaders
