#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Frame.hpp — Contextes (repères de référence) façon BRUTAL / KSA
//
//  « Everything's contextual, so an object is drawn relative to something
//    else. » — au lieu d'une origine flottante UNIQUE (tout est stocké en
//  absolu puis on soustrait la caméra), chaque entité est stockée
//  RELATIVEMENT à son contexte, et le rendu résout la chaîne.
//
//  ── Pourquoi c'est mieux qu'un floating origin plat ──────────────────
//  Un double a ~15-16 chiffres significatifs. Stocker une position
//  absolue à 30 UA (4.5e9 km) plafonne la résolution à ~1e-6 km ≈ 1 mm :
//  impossible de poser un vaisseau au millimètre près sur Neptune.
//  Stocké dans le contexte « Neptune », le même point vaut ~1e3 km et se
//  résout à ~1e-13 km. La précision devient INDÉPENDANTE de la distance
//  absolue à l'étoile.
//
//  ── Structure ────────────────────────────────────────────────────────
//  Un Frame n'a pas de position propre : son origine EST la position du
//  corps qui l'ancre (anchor), exprimée dans le frame parent. Pas d'état
//  dupliqué, donc rien à resynchroniser.
//
//      racine (barycentre, inertiel)
//        └── Soleil ── Terre ── Lune
//                   └── Mars
//
//  Les frames ne tournent pas : ce sont des translations pures. Passer
//  d'un contexte à l'autre est donc une simple addition de vecteurs —
//  exact, continu, et sans « pop » visuel (d'où l'absence d'écrans de
//  chargement quand on change de référentiel).
// ════════════════════════════════════════════════════════════════════════

#include "Body.hpp"
#include "Constants.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cmath>

struct Frame {
    std::string name;
    int    parent = -1;    // index du frame parent (-1 = racine)
    int    anchor = -1;    // index du Body dont la position définit l'origine
    double soi    = 0.0;   // rayon de la sphère d'influence (km), 0 = infini
};

class FrameGraph {
public:
    std::vector<Frame> frames;

    // ── Construction ─────────────────────────────────────────────────
    int addRoot(const char* name) {
        frames.push_back(Frame{ name, -1, -1, 0.0 });
        return (int)frames.size() - 1;
    }

    // Ajoute un contexte ancré sur un corps. La SOI est calculée par la
    // formule classique r = a * (m/M)^(2/5).
    int add(const char* name, int parent, int anchorBody,
            double anchorDist, double anchorMass, double parentMass)
    {
        double soi = 0.0;
        if (anchorDist > 0.0 && parentMass > 0.0 && anchorMass > 0.0)
            soi = anchorDist * std::pow(anchorMass / parentMass, 0.4);
        frames.push_back(Frame{ name, parent, anchorBody, soi });
        return (int)frames.size() - 1;
    }

    int  size()   const { return (int)frames.size(); }
    bool valid(int f) const { return f >= 0 && f < (int)frames.size(); }
    const std::string& name(int f) const { return frames[f].name; }

    // Contexte porté par ce corps (-1 s'il n'en ancre aucun).
    // Suivre un corps revient à entrer dans SON contexte : la cible de la
    // caméra devient exactement (0,0,0) et n'a plus jamais besoin d'être
    // recalculée, au lieu de poursuivre une coordonnée absolue mouvante.
    int frameAnchoredTo(int bodyIndex) const {
        for (int f = 0; f < (int)frames.size(); ++f)
            if (frames[f].anchor == bodyIndex) return f;
        return -1;
    }

    // ── Généalogie ───────────────────────────────────────────────────
    int depth(int f) const {
        int d = 0;
        while (valid(f) && frames[f].parent >= 0) { f = frames[f].parent; ++d; }
        return d;
    }

    int commonAncestor(int a, int b) const {
        if (!valid(a) || !valid(b)) return -1;
        int da = depth(a), db = depth(b);
        while (da > db) { a = frames[a].parent; --da; }
        while (db > da) { b = frames[b].parent; --db; }
        while (a != b && a >= 0 && b >= 0) {
            a = frames[a].parent;
            b = frames[b].parent;
        }
        return a;
    }

    // ── Résolution de chaîne ─────────────────────────────────────────
    // Somme des origines en remontant de `f` jusqu'à `stop` (exclu).
    // Chaque terme est une quantité LOCALE (petite), jamais une coordonnée
    // absolue : c'est là que se gagne la précision.
    glm::dvec3 climb(int f, int stop, const std::vector<Body>& bodies) const {
        glm::dvec3 acc(0.0);
        while (valid(f) && f != stop) {
            int a = frames[f].anchor;
            if (a >= 0 && a < (int)bodies.size()) acc += bodies[a].pos;
            f = frames[f].parent;
        }
        return acc;
    }

    glm::dvec3 climbVel(int f, int stop, const std::vector<Body>& bodies) const {
        glm::dvec3 acc(0.0);
        while (valid(f) && f != stop) {
            int a = frames[f].anchor;
            if (a >= 0 && a < (int)bodies.size()) acc += bodies[a].vel;
            f = frames[f].parent;
        }
        return acc;
    }

    // Position de l'origine du frame `from`, exprimée dans le frame `to`.
    glm::dvec3 originOffset(int from, int to, const std::vector<Body>& bodies) const {
        if (from == to) return glm::dvec3(0.0);
        int ca = commonAncestor(from, to);
        return climb(from, ca, bodies) - climb(to, ca, bodies);
    }

    glm::dvec3 originVelocity(int from, int to, const std::vector<Body>& bodies) const {
        if (from == to) return glm::dvec3(0.0);
        int ca = commonAncestor(from, to);
        return climbVel(from, ca, bodies) - climbVel(to, ca, bodies);
    }

    // ── Conversions de contexte ──────────────────────────────────────
    // Un point local au frame `from`, réexprimé dans le frame `to`.
    glm::dvec3 convert(const glm::dvec3& p, int from, int to,
                       const std::vector<Body>& bodies) const {
        return p + originOffset(from, to, bodies);
    }

    glm::dvec3 convertVel(const glm::dvec3& v, int from, int to,
                          const std::vector<Body>& bodies) const {
        return v + originVelocity(from, to, bodies);
    }

    // Vecteur allant du point (a, frameA) au point (b, frameB), exprimé
    // dans les axes de frameA. C'est LA primitive du moteur : elle ne
    // construit jamais de coordonnée absolue.
    glm::dvec3 separation(const glm::dvec3& a, int frameA,
                          const glm::dvec3& b, int frameB,
                          const std::vector<Body>& bodies) const {
        return convert(b, frameB, frameA, bodies) - a;
    }

    // Position dans le frame racine — DIAGNOSTIC / UI uniquement.
    // C'est précisément la quantité qu'on évite de manipuler en interne.
    glm::dvec3 toRoot(const glm::dvec3& p, int frame,
                      const std::vector<Body>& bodies) const {
        return p + climb(frame, -1, bodies);
    }

    // ── Changement de contexte (sphères d'influence) ─────────────────
    // Rend le contexte le plus profond dont la SOI contient ce point.
    // C'est ce qui permet de traverser un système sans discontinuité :
    // la conversion étant une translation exacte, rien ne « saute ».
    //
    // `excludeAnchor` : index d'un corps dont on ignore le propre contexte.
    // Un corps qui ancre un frame est à distance nulle de son origine, il
    // « tomberait » sinon dans sa propre SOI. Corollaire : les corps-ancres
    // ne changent jamais de contexte — seules les entités libres (caméra,
    // vaisseaux) en changent.
    int bestFrameFor(const glm::dvec3& p, int currentFrame,
                     const std::vector<Body>& bodies,
                     int excludeAnchor = -1) const
    {
        int best = currentFrame, bestDepth = -1;
        for (int f = 0; f < (int)frames.size(); ++f) {
            if (frames[f].soi <= 0.0) continue;             // racine / Soleil
            if (excludeAnchor >= 0 && frames[f].anchor == excludeAnchor) continue;
            glm::dvec3 local = convert(p, currentFrame, f, bodies);
            if (glm::length(local) < frames[f].soi) {
                int d = depth(f);
                if (d > bestDepth) { bestDepth = d; best = f; }
            }
        }
        if (bestDepth < 0) {
            // Hors de toute SOI : remonter au frame le moins profond non racine
            for (int f = 0; f < (int)frames.size(); ++f)
                if (frames[f].parent == 0) return f;        // frame de l'étoile
        }
        return best;
    }

    // Déplace une entité dans un autre contexte SANS la faire bouger.
    void reparent(glm::dvec3& pos, glm::dvec3& vel, int& frame, int newFrame,
                  const std::vector<Body>& bodies) const
    {
        if (frame == newFrame || !valid(newFrame)) return;
        pos   = convert(pos, frame, newFrame, bodies);
        vel   = convertVel(vel, frame, newFrame, bodies);
        frame = newFrame;
    }
};
