#pragma once

// ════════════════════════════════════════════════════════════════════════
//  SurfaceGLSL.hpp — Génération procédurale des surfaces (SOLID: SRP, OCP)
//
//  ── Le principe : un GÉNOME par planète ───────────────────────────────
//  Une graine qui se contente de décaler le domaine du bruit ne produit
//  pas de la variété : elle produit la MÊME planète vue ailleurs. Tous les
//  mondes d'une catégorie se ressemblent alors, à la couleur près.
//
//  Ici la graine engendre une douzaine de PARAMÈTRES DE STRUCTURE :
//  échelle des continents, force du relief, densité de cratères, ampleur
//  des canyons, étendue des calottes, contraste, nombre de bandes… Deux
//  mondes désertiques n'ont donc ni la même géologie, ni le même âge
//  apparent — l'un est un désert de dunes lisse, l'autre un highland
//  criblé de cratères avec de grands bassins basaltiques.
//
//  La COULEUR, elle, reste dans la famille de la teinte du corps : c'est
//  ce qui fait qu'on reconnaît « un Mars » tout en n'en voyant jamais deux
//  identiques.
//
//  Quatre catégories, une fonction chacune. En ajouter une = ajouter une
//  fonction et une branche, sans toucher aux autres.
// ════════════════════════════════════════════════════════════════════════

namespace Shaders {

static const char* SURFACE = R"GLSL(
// ── Gène n° k d'une graine, dans [0,1] ──────────────────────────────────
float gene(float seed, float k) {
    return hash11(seed * 2.718 + k * 19.73 + 0.5);
}
// Gène remis dans un intervalle
float geneR(float seed, float k, float lo, float hi) {
    return mix(lo, hi, gene(seed, k));
}

// ── Décalage de domaine, BORNÉ ──────────────────────────────────────────
// Décaler le domaine par « seed * 219.3 » porte les coordonnées à ~2e5 ;
// après dix octaves (x553) on est à 1e8, où l'ulp du float32 vaut 8 pour
// une maille de bruit de 1. Les octaves fines sont alors quantifiées en
// marches, ce qui produit à l'écran de larges anneaux concentriques.
// Le décalage doit rester petit : il ne sert qu'à décorréler les mondes.
vec3 seedOffset(float seed) {
    return vec3(gene(seed, 41.0), gene(seed, 42.0), gene(seed, 43.0)) * 16.0;
}

// ════════════════════════════════════════════════════════════════════════
//  0 — TELLURIQUE HABITABLE : océans, continents, biomes, calottes
// ════════════════════════════════════════════════════════════════════════
vec3 surfTerrestrial(vec3 n, float seed, vec3 tint, int oct, out float gloss)
{
    // ── Génome ──────────────────────────────────────────────────────────
    float gScale   = geneR(seed,  1.0, 1.1, 3.4);   // taille des continents
    float gWarp    = geneR(seed,  2.0, 0.25, 0.95); // sinuosité des côtes
    float gSea     = geneR(seed,  3.0, 0.34, 0.64); // niveau des mers
    float gMount   = geneR(seed,  4.0, 0.10, 0.70); // force des chaînes
    float gArid    = geneR(seed,  5.0, 0.10, 0.85); // part de désert
    float gIce     = geneR(seed,  6.0, 0.55, 0.95); // étendue des calottes
    float gLush    = geneR(seed,  7.0, 0.0,  1.0);  // luxuriance
    float gHue     = geneR(seed,  8.0, -0.55, 0.55);

    vec3 p = n * gScale + seedOffset(seed);

    // Continents : fBm déformé -> côtes découpées, pas des taches rondes
    vec3  q = warp(p, gWarp, max(3, oct - 3));
    float h = fbm(q, oct) * 0.5 + 0.5;

    // Détail côtier : les extrema de Perlin se placent SUR la grille, donc
    // ses iso-contours s'y alignent — et un trait de côte EST une
    // iso-contour, d'où des marches rectangulaires. Une octave fine rend
    // le contour fractal et casse l'alignement.
    h += 0.045 * fbm(q * 8.7 + 4.1, max(3, oct - 2));

    float lat   = abs(n.y);
    float polar = smoothstep(gIce, gIce + 0.16, lat + 0.12 * fbm(p * 3.1, 3));

    // ── Océans ──────────────────────────────────────────────────────────
    if (h < gSea) {
        float depth   = (gSea - h) / max(gSea, 1e-4);
        vec3  deep    = vec3(0.012, 0.045, 0.16);
        vec3  shallow = vec3(0.05,  0.34,  0.52);
        vec3  c       = mix(shallow, deep, smoothstep(0.0, 0.45, depth));
        c     = mix(c, vec3(0.86, 0.91, 0.96), polar);   // banquise
        gloss = mix(0.75, 0.05, polar);                  // l'eau brille
        return hueShift(c, gHue * 0.5);
    }

    // ── Terres émergées ─────────────────────────────────────────────────
    float e = (h - gSea) / max(1.0 - gSea, 1e-4);

    float mountains = ridged(q * 2.7, max(3, oct - 2));
    e = clamp(e + mountains * gMount * smoothstep(0.20, 0.8, e), 0.0, 1.0);

    // Aridité : ceintures désertiques vers 25-30° de latitude, là où
    // redescendent les cellules de Hadley.
    float band = exp(-pow((lat - 0.45) / 0.17, 2.0));
    float arid = clamp((fbm(p * 2.3 + 31.7, max(3, oct - 2)) * 0.5 + 0.5)
                       * (0.30 + gArid) + band * gArid, 0.0, 1.0);

    vec3 jungle = mix(vec3(0.13, 0.26, 0.10), vec3(0.06, 0.30, 0.05), gLush);
    vec3 grass  = mix(vec3(0.30, 0.38, 0.18), vec3(0.18, 0.44, 0.14), gLush);
    vec3 sand   = vec3(0.74, 0.64, 0.40);
    vec3 rock   = vec3(0.36, 0.33, 0.29);
    vec3 snow   = vec3(0.93, 0.95, 0.97);

    vec3 c = mix(jungle, grass, smoothstep(0.05, 0.40, lat));
    c = mix(c, sand, smoothstep(0.40, 0.80, arid));
    c = mix(vec3(0.80, 0.74, 0.55), c, smoothstep(0.0, 0.075, e));   // plage
    c = mix(c, rock, smoothstep(0.38, 0.70, e));
    float snowLine = 0.70 - 0.50 * lat;
    c = mix(c, snow, smoothstep(snowLine, snowLine + 0.10, e));
    c = mix(c, snow, polar);

    c = hueShift(c, gHue * 0.35);
    c = mix(c, c * (tint / max(luminance(tint), 0.15)), 0.20);

    gloss = 0.03;
    return c;
}

// ════════════════════════════════════════════════════════════════════════
//  1 — DÉSERTIQUE : une teinte dominante, des géologies très différentes
// ════════════════════════════════════════════════════════════════════════
vec3 surfDesert(vec3 n, float seed, vec3 tint, int oct, out float gloss)
{
    // ── Génome : c'est lui qui distingue un monde de dunes lisse d'un
    //    highland criblé de cratères ou d'un désert taillé de canyons.
    float gScale  = geneR(seed,  1.0, 1.2, 3.6);
    float gMaria  = geneR(seed,  2.0, 0.0, 1.0);   // grands bassins sombres
    float gCrater = geneR(seed,  3.0, 0.0, 1.0);   // âge / bombardement
    float gCanyon = geneR(seed,  4.0, 0.0, 1.0);   // tectonique
    float gDune   = geneR(seed,  5.0, 0.0, 1.0);   // couverture sableuse
    float gCaps   = geneR(seed,  6.0, 0.0, 1.0);   // volatils polaires
    float gContr  = geneR(seed,  7.0, 0.45, 1.35); // contraste d'albédo
    float gHue    = geneR(seed,  8.0, -0.30, 0.30);
    float gOxide  = geneR(seed,  9.0, 0.2, 1.0);

    vec3 p = n * gScale + seedOffset(seed);

    // Grandes structures : hauts plateaux clairs / bassins d'impact sombres
    float region = fbm(warp(p, 0.45, 3), oct) * 0.5 + 0.5;
    float maria  = smoothstep(0.52, 0.30, region) * gMaria;

    // Cratères : bourrelet clair, cuvette sombre. Sans bruit cellulaire on
    // ne sait pas les faire — un fBm ne produit pas d'anneaux.
    float cr = craters(p * 3.1 + 40.0, gCrater)
             + craters(p * 8.7 + 91.0, gCrater * 0.6) * 0.5;

    // Canyons : réseau de failles ramifiées
    float canyon = smoothstep(0.74, 0.99, ridged(p * 1.9 + 12.3, max(3, oct - 2)))
                 * gCanyon;

    // Dunes : billow fortement anisotrope (vents zonaux)
    vec3  dp    = vec3(p.x * 0.30, p.y * 5.0, p.z * 0.30);
    float dunes = billow(dp * 3.2, max(3, oct - 3)) * gDune;

    float oxide = (fbm(p * 5.1 + 77.0, max(3, oct - 3)) * 0.5 + 0.5) * gOxide;

    // ── Palette : la teinte du corps RESTE dominante ────────────────────
    // On module luminosité et saturation autour d'elle plutôt que de la
    // délaver vers le blanc, sinon toutes les planètes virent au beige.
    float L    = max(luminance(tint), 0.08);
    vec3  base = tint / L;
    vec3  dark = base * (0.30 / gContr);            // basaltes
    vec3  pale = mix(base, vec3(1.0), 0.16) * gContr;   // peu délavé

    vec3 c = mix(dark, base, smoothstep(0.28, 0.70, region));
    c = mix(c, pale, smoothstep(0.62, 0.95, region));
    c = mix(c, dark * 0.9, maria);                  // mers basaltiques
    c *= mix(1.0, 1.18, dunes);                     // ondulation des dunes
    c = mix(c, dark * 0.75, canyon);                // ombre des canyons
    c = mix(c, pale, clamp(cr, 0.0, 1.0) * 0.55);   // bourrelets
    c = mix(c, dark, clamp(-cr, 0.0, 1.0) * 0.45);  // fonds de cratère
    c *= mix(0.86, 1.14, oxide);

    // Calottes de volatils, bord irrégulier — certains mondes n'en ont pas
    if (gCaps > 0.35) {
        float lim  = mix(0.94, 0.72, gCaps);
        float caps = smoothstep(lim, lim + 0.12, abs(n.y) + 0.13 * fbm(p * 4.0, 3));
        c = mix(c, vec3(0.94, 0.94, 0.92), caps);
    }

    c = hueShift(c, gHue);
    gloss = 0.02;
    return c * L * 1.5;
}

// ════════════════════════════════════════════════════════════════════════
//  2 — GLACÉE : banquise fracturée, crevasses, dépôts organiques
// ════════════════════════════════════════════════════════════════════════
vec3 surfIcy(vec3 n, float seed, vec3 tint, int oct, out float gloss)
{
    float gScale  = geneR(seed, 1.0, 1.4, 4.0);
    float gCrack  = geneR(seed, 2.0, 0.15, 1.0);   // activité tectonique
    float gPlates = geneR(seed, 3.0, 0.0, 1.0);    // banquise en plaques
    float gTholin = geneR(seed, 4.0, 0.0, 0.85);   // dépôts organiques
    float gCrater = geneR(seed, 5.0, 0.0, 0.7);
    float gHue    = geneR(seed, 6.0, -0.4, 0.4);

    vec3 p = n * gScale + seedOffset(seed);

    // Fractures fines à la Europe : ridged pour les linéaments, Worley
    // F2-F1 pour les joints de plaques.
    float lin = smoothstep(0.80, 0.99, ridged(p * 1.5, max(3, oct - 2)))
              + 0.55 * smoothstep(0.87, 1.0, ridged(p * 4.1 + 41.0, max(3, oct - 3)));
    vec2  w   = worley(p * 2.2 + 7.0);
    float joints = smoothstep(0.12, 0.0, w.y - w.x) * gPlates;
    float cracks = clamp(lin * gCrack + joints, 0.0, 1.0);

    float relief = fbm(p * 2.0, oct) * 0.5 + 0.5;
    float cr     = craters(p * 4.5 + 130.0, gCrater);

    vec3 ice    = vec3(0.88, 0.93, 0.97);
    vec3 shadow = vec3(0.55, 0.68, 0.81);
    vec3 tholin = mix(vec3(0.72, 0.55, 0.38), tint, 0.55);

    vec3 c = mix(shadow, ice, smoothstep(0.32, 0.74, relief));
    c = mix(c, tholin, cracks * (0.30 + gTholin * 0.65));
    c = mix(c, vec3(0.97, 0.99, 1.0), smoothstep(0.80, 0.96, relief));  // givre
    c = mix(c, vec3(0.99), clamp(cr, 0.0, 1.0) * 0.5);
    // Teinte globale du corps (Europe blanche, Titan orangé)
    c = mix(c, c * (tint / max(luminance(tint), 0.15)), 0.35);

    c = hueShift(c, gHue * 0.4);
    gloss = 0.35;
    return c;
}

// ════════════════════════════════════════════════════════════════════════
//  3 — GÉANTE GAZEUSE : bandes zonales, turbulence, tourbillons
// ════════════════════════════════════════════════════════════════════════
vec3 surfGasGiant(vec3 n, float seed, vec3 tint, int oct, out float gloss)
{
    float gBands  = geneR(seed, 1.0,  6.0, 26.0);  // nombre de bandes
    float gTurb   = geneR(seed, 2.0,  1.5,  9.0);  // turbulence
    float gStorm  = geneR(seed, 3.0,  0.0,  1.0);  // densité de tourbillons
    float gContr  = geneR(seed, 4.0,  0.35, 1.0);  // contraste des bandes
    float gShear  = geneR(seed, 5.0,  3.0, 12.0);  // cisaillement zonal
    float gHue    = geneR(seed, 6.0, -0.35, 0.35);
    float gPole   = geneR(seed, 7.0,  0.55, 0.95); // assombrissement polaire

    vec3 p = n * 2.0 + seedOffset(seed);

    // On écrase fortement l'axe Y : le bruit s'étire alors en bandes
    // horizontales, comme un écoulement zonal réel.
    vec3  fp   = vec3(p.x, p.y * gShear, p.z);
    float turb = fbm(fp * 1.6, oct) * 0.5 + 0.5;

    float lat  = n.y;
    float band = sin(lat * gBands + turb * 5.0 + gene(seed, 44.0) * 6.28) * 0.5 + 0.5;
    band = smoothstep(0.5 - gContr * 0.5, 0.5 + gContr * 0.5, band);

    float L    = max(luminance(tint), 0.08);
    vec3  base = tint / L;
    vec3  belt = base * 0.58;
    vec3  zone = mix(base, vec3(1.0), 0.38) * 1.10;

    vec3 c = mix(belt, zone, band);

    float fil = billow(fp * 6.0 + 17.0, max(3, oct - 3));
    c = mix(c, c * mix(0.84, 1.18, fil), 0.45);

    // Tourbillons ovales : Worley aplati donne de vraies cellules fermées,
    // là où un ridged ne donnait que des filaments.
    vec2  wv = worley(vec3(p.x * 1.6, p.y * gShear * 0.9, p.z * 1.6) + 91.0);
    float ovals = smoothstep(0.30, 0.05, wv.x) * gStorm
                * exp(-pow((abs(lat) - 0.35) / 0.30, 2.0));
    c = mix(c, hueShift(base, 1.1) * 1.30, ovals * 0.8);

    c *= mix(1.0, gPole, smoothstep(0.5, 1.0, abs(lat)));
    c = hueShift(c, gHue * 0.3);

    gloss = 0.0;
    return c * L * 1.5;
}

// ════════════════════════════════════════════════════════════════════════
//  Aiguillage
// ════════════════════════════════════════════════════════════════════════
vec3 surfaceColor(int type, float seed, vec3 tint, vec3 nObj, int oct,
                  out float gloss)
{
    if (type == 1) return surfDesert(nObj, seed, tint, oct, gloss);
    if (type == 2) return surfIcy(nObj, seed, tint, oct, gloss);
    if (type == 3) return surfGasGiant(nObj, seed, tint, oct, gloss);
    return surfTerrestrial(nObj, seed, tint, oct, gloss);
}
)GLSL";

} // namespace Shaders
