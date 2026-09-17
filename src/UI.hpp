#pragma once

// ════════════════════════════════════════════════════════════════════════
//  UI.hpp — Panneau ImGui (SOLID: SRP)
//  Responsabilité unique : afficher et modifier les paramètres.
// ════════════════════════════════════════════════════════════════════════

#include "imgui.h"
#include "Body.hpp"
#include "Frame.hpp"
#include "Camera.hpp"
#include "Navigation.hpp"
#include "VisualScale.hpp"
#include "Surface.hpp"
#include "Simulation.hpp"
#include "Physics.hpp"
#include "Constants.hpp"

#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>

class UI {
public:
    int   followBodyIndex = -1;   // -1 = aucun suivi
    float exposure        = 1.f;
    bool  autoContext     = true; // bascule de contexte par sphère d'influence

    // ── Dimensionnement visuel ────────────────────────────────────────
    VisualScale::Mode scaleMode  = VisualScale::Mode::Schematique;
    float             scaleK     = VisualScale::K_DEFAULT;
    bool              scaleDirty = false;   // main recalcule et remet à false
    bool              surfaceDirty = false; // main recuit le cache et remet à false
    bool              useCache     = true;
    bool              showClouds   = true;

    // Retourne true si la caméra doit être rafraîchie
    bool draw(Camera& cam, std::vector<Body>& bodies, const FrameGraph& fg,
              Simulation& sim, float fps, float screenH, int drawnBodies,
              const Navigation::Nearest& nearInfo,
              const std::string& cacheInfo)
    {
        bool camDirty = false;
        const bool freeMode = (cam.mode == Camera::Mode::Free);

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Always);
        ImGui::Begin("Solar System", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Temps simule : %s",
            formatTime(sim.simTime).c_str());
        ImGui::Separator();

        // ══ Caméra ═══════════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (freeMode)
                ImGui::TextColored(ImVec4(0.4f, 1, 0.6f, 1), "Mode : VOL LIBRE");
            else
                ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Mode : ORBITE");
            ImGui::SameLine();
            if (ImGui::SmallButton("Basculer (V)")) m_toggleRequested = true;

            if (freeMode) {
                double v = Navigation::freeFlySpeed(nearInfo.altitude, cam.speedBoost);
                ImGui::Text("Vitesse : %s", formatSpeed(v).c_str());
                ImGui::TextDisabled("ZQSD/WASD deplacer | Espace/C monter-descendre");
                ImGui::TextDisabled("Maj turbo x5 | Ctrl precision x0.2 | Molette reglage");
                ImGui::SliderFloat("Multiplicateur", &cam.speedBoost, 0.01f, 100.f,
                                   "%.2f", ImGuiSliderFlags_Logarithmic);
                if (nearInfo.index >= 0)
                    ImGui::Text("Corps le plus proche : %s  (altitude %s)",
                        bodies[nearInfo.index].name.c_str(),
                        formatDistance(nearInfo.altitude).c_str());
            } else {
                camDirty |= ImGui::SliderFloat("Azimut", &cam.theta,
                    -glm::pi<float>(), glm::pi<float>());
                camDirty |= ImGui::SliderFloat("Elevation", &cam.phi,
                    -glm::half_pi<float>() + 0.05f,
                     glm::half_pi<float>() - 0.05f);
                float distAU = cam.distance / float(Constants::AU_KM);
                if (ImGui::SliderFloat("Distance (UA)", &distAU, 1e-7f, 60.f,
                                       "%.7f", ImGuiSliderFlags_Logarithmic)) {
                    cam.distance = distAU * float(Constants::AU_KM);
                    camDirty = true;
                }
                ImGui::TextDisabled("   = %s", formatDistance(cam.distance).c_str());
            }

            camDirty |= ImGui::SliderFloat("FOV", &cam.fov, 5.f, 120.f);
            ImGui::SliderFloat("Exposition", &exposure, 0.1f, 4.f);

            // ── Cible ──────────────────────────────────────────────────
            const char* current = (followBodyIndex >= 0 &&
                                   followBodyIndex < (int)bodies.size())
                                ? bodies[followBodyIndex].name.c_str()
                                : (freeMode ? "(vol libre)" : "(aucune)");
            if (ImGui::BeginCombo("Cibler (suivre)", current)) {
                for (int i = 0; i < (int)bodies.size(); ++i) {
                    if (ImGui::Selectable(bodies[i].name.c_str(), followBodyIndex == i)) {
                        // Entre dans le contexte du corps : sa cible devient
                        // (0,0,0), donc il est au centre EXACT de l'écran.
                        Navigation::focusOn(cam, fg, bodies, i, false);
                        cam.distance    = Navigation::framingDistance(bodies[i]);
                        followBodyIndex = i;
                        camDirty        = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (!freeMode && followBodyIndex >= 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Lacher")) followBodyIndex = -1;
            }
        }
        ImGui::Separator();

        // ══ Taille des corps ═════════════════════════════════════════
        if (ImGui::CollapsingHeader("Taille des corps", ImGuiTreeNodeFlags_DefaultOpen)) {
            int m = (int)scaleMode;
            const char* items[] = { "Reel (1:1)",
                                    "Coherent (sans chevauchement)",
                                    "Schematique (hierarchie preservee)" };
            if (ImGui::Combo("Mode", &m, items, 3)) {
                scaleMode  = (VisualScale::Mode)m;
                scaleDirty = true;
            }
            if (scaleMode == VisualScale::Mode::Schematique) {
                if (ImGui::SliderFloat("Grossissement K", &scaleK, 0.f, 120000.f, "%.0f"))
                    scaleDirty = true;
                ImGui::TextDisabled("r_visuel = K * sqrt(r_reel) : comprime la");
                ImGui::TextDisabled("dynamique sans inverser l'ordre des tailles.");
            }
            switch (scaleMode) {
                case VisualScale::Mode::Reel:
                    ImGui::TextDisabled("Echelle physique exacte. Les planetes sont");
                    ImGui::TextDisabled("invisibles de loin : passez en vol libre (V).");
                    break;
                case VisualScale::Mode::Coherent:
                    ImGui::TextDisabled("Chaque corps grossi au maximum sans empieter");
                    ImGui::TextDisabled("sur son voisin. La Lune reste hors de la Terre.");
                    break;
                case VisualScale::Mode::Schematique:
                    ImGui::TextDisabled("Vue d'ensemble. A fort K la Lune passe sous");
                    ImGui::TextDisabled("la surface de la Terre : c'est une carte.");
                    break;
            }
        }
        ImGui::Separator();

        // ══ Contexte ═════════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Contexte", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1, 1), "Camera dans : %s",
                fg.valid(cam.frame) ? fg.name(cam.frame).c_str() : "?");
            ImGui::Text("Position locale : %.3f  %.3f  %.3f km",
                        cam.pos.x, cam.pos.y, cam.pos.z);

            glm::dvec3 abs = fg.toRoot(cam.pos, cam.frame, bodies);
            ImGui::TextDisabled("Absolu (diagnostic) : %.4f UA",
                                glm::length(abs) / Constants::AU_KM);

            // Comparaison de résolution : tout l'argument du modèle.
            double locMag = std::max(1.0, glm::length(cam.pos));
            double absMag = std::max(1.0, glm::length(abs));
            ImGui::Text("Resolution float64 ici : %.2e km", ulpOf(locMag));
            ImGui::TextDisabled("   (en absolu : %.2e km, soit %.0fx pire)",
                ulpOf(absMag), ulpOf(absMag) / ulpOf(locMag));

            ImGui::Checkbox("Bascule auto par sphere d'influence", &autoContext);
            ImGui::TextDisabled("Pour VISER un corps, utilisez \"Cibler\" dans");
            ImGui::TextDisabled("la section Camera. Le reglage ci-dessous ne");
            ImGui::TextDisabled("change que le repere de calcul (diagnostic).");
            if (ImGui::BeginCombo("Repere de calcul",
                    fg.valid(cam.frame) ? fg.name(cam.frame).c_str() : "?")) {
                for (int f = 0; f < fg.size(); ++f) {
                    if (ImGui::Selectable(fg.name(f).c_str(), cam.frame == f)) {
                        // Translation exacte : la caméra ne bouge pas d'un mm
                        glm::dvec3 v(0.0);
                        int nf = cam.frame;
                        cam.target = fg.convert(cam.target, cam.frame, f, bodies);
                        fg.reparent(cam.pos, v, nf, f, bodies);
                        cam.frame  = f;
                        autoContext = false;
                        // Le pivot suit le repère : sinon la caméra zoome
                        // vers un point que le corps quitte.
                        if (cam.mode == Camera::Mode::Orbit &&
                            fg.frames[f].anchor >= 0)
                            cam.toOrbit(glm::dvec3(0.0));
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();

        // ══ Simulation ═══════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Pause (P)", &sim.paused); ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                sim.reset(bodies, m_initialBodies);
                followBodyIndex = -1;
                cam.frame   = fg.size() > 1 ? 1 : 0;
                cam.target  = glm::dvec3(0);
                scaleDirty  = true;
                camDirty    = true;
            }

            float dtD = float(sim.deltaT / Constants::DAY_S);
            if (ImGui::SliderFloat("DeltaT (jours/frame)", &dtD, 0.001f, 365.f,
                                   "%.3f", ImGuiSliderFlags_Logarithmic))
                sim.deltaT = double(dtD) * Constants::DAY_S;
            ImGui::SliderInt("Sous-etapes/frame", &sim.stepsPerFrame, 1, 400);

            double sub  = sim.subStepSeconds();
            double reco = Physics::shortestDynamicalTime(bodies, fg) / 20.0;
            ImGui::Text("Sous-pas : %.0f s", sub); ImGui::SameLine();
            if (reco > 0.0 && sub > reco)
                ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1),
                    "  INSTABLE (< %.0f s recommande)", reco);
            else
                ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "  stable");

            if (m_energy0 != 0.0) {
                double e = Physics::totalEnergy(bodies, fg);
                ImGui::Text("Derive d'energie : %+.3e",
                            (e - m_energy0) / std::abs(m_energy0));
            }
        }
        ImGui::Separator();

        // ══ Corps célestes ═══════════════════════════════════════════
        if (ImGui::CollapsingHeader("Corps celestes")) {
            ImGui::BeginChild("bodies", ImVec2(0, 260), true);
            for (int i = 0; i < (int)bodies.size(); ++i) {
                Body& b = bodies[i];
                ImGui::PushID(i);
                if (ImGui::TreeNode(b.name.c_str())) {
                    ImGui::TextColored(ImVec4(0.5f, 0.85f, 1, 1), "Contexte : %s",
                        fg.valid(b.frame) ? fg.name(b.frame).c_str() : "?");
                    ImGui::Text("Pos locale : %.3f  %.3f  %.3f km",
                                b.pos.x, b.pos.y, b.pos.z);
                    ImGui::Text("Vit. locale : %.4f km/s", glm::length(b.vel));
                    ImGui::TextDisabled("Vit. absolue : %.4f km/s",
                        glm::length(Physics::absoluteVel(b, fg, bodies)));
                    ImGui::Text("Rayon reel : %.0f km", b.radius);
                    ImGui::Text("Rayon rendu : %.0f km  (x%.1f)",
                                double(b.visualRadius()), double(b.visualScale));
                    int fr = fg.frameAnchoredTo(i);
                    if (fr >= 0 && fg.frames[fr].soi > 0.0)
                        ImGui::Text("SOI : %.4e km", fg.frames[fr].soi);
                    // ── Surface procédurale ────────────────────────
                    if (b.emissive < 0.5f) {
                        int st = b.surfaceType;
                        if (ImGui::Combo("Categorie", &st, Surface::names(), Surface::COUNT)) {
                            b.surfaceType = st; surfaceDirty = true;
                        }
                        if (ImGui::SliderFloat("Graine", &b.surfaceSeed, 0.f, 1000.f, "%.2f"))
                            surfaceDirty = true;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Tirer")) {
                            b.surfaceSeed = float((m_rng = m_rng * 1103515245u + 12345u)
                                                  % 100000u) * 0.01f;
                            surfaceDirty = true;
                        }
                    }
                    if (ImGui::ColorEdit3("Teinte", &b.color.x)) surfaceDirty = true;
                    ImGui::TextDisabled("La palette est derivee sur le GPU de");
                    ImGui::TextDisabled("(categorie, graine, teinte).");
                    if (ImGui::Button("Cibler")) {
                        Navigation::focusOn(cam, fg, bodies, i, false);
                        cam.distance    = Navigation::framingDistance(b);
                        followBodyIndex = i;
                        camDirty        = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Tracer l'orbite")) sim.setTrailBody(i, bodies);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::Separator();

        ImGui::Checkbox("Afficher trail orbite", &sim.showTrail);
        if (sim.trailBody >= 0 && sim.trailBody < (int)bodies.size()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s dans %s, %d pts)",
                bodies[sim.trailBody].name.c_str(),
                fg.valid(sim.trailFrame) ? fg.name(sim.trailFrame).c_str() : "?",
                (int)sim.trail.size());
        }
        ImGui::Separator();

        // ══ Debug ════════════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Debug technique")) {
            ImGui::Text("Corps envoyes au GPU : %d / %d", drawnBodies, (int)bodies.size());
            ImGui::TextDisabled("(inclut les occulteurs hors champ)");
            if (ImGui::Checkbox("Cache de surfaces", &useCache)) { /* main applique */ }
            ImGui::SameLine();
            ImGui::Checkbox("Nuages", &showClouds);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", cacheInfo.c_str());
            glm::dvec3 bc = Physics::barycenter(bodies, fg);
            ImGui::Text("Barycentre : %.4e km", glm::length(bc));
            ImGui::Separator();
            ImGui::Text("Arbre des contextes :");
            drawFrameTree(fg, -1, 0, cam.frame);
            ImGui::Separator();
            ImGui::BulletText("Simulation : float64, relative au contexte");
            ImGui::BulletText("Rendu : float32, camera a l'origine");
            ImGui::BulletText("Aucune coordonnee absolue en interne");
            ImGui::BulletText("Ray casting analytique 100%% GPU");
        }

        ImGui::Separator();
        ImGui::Text("FPS : %.1f", fps);
        ImGui::End();

        // ── Barre d'aide ──────────────────────────────────────────────
        // screenH est en POINTS logiques ImGui, pas en pixels framebuffer
        // (les deux diffèrent d'un facteur 2 sur Retina).
        ImGui::SetNextWindowPos(ImVec2(10, screenH - 44), ImGuiCond_Always);
        ImGui::Begin("##help", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        if (freeMode)
            ImGui::TextDisabled("V : mode orbite   |   WASD + Espace/C : voler   |"
                                "   Maj : turbo   |   Molette : vitesse   |   P : pause");
        else
            ImGui::TextDisabled("V : vol libre   |   Clic G + glisser : orbite   |"
                                "   Molette : zoom   |   Espace ou P : pause");
        ImGui::End();

        return camDirty;
    }

    // Le bouton « Basculer » de l'UI passe par ce drapeau pour que main
    // reste le seul endroit qui pilote le changement de mode.
    bool consumeToggleRequest() {
        bool t = m_toggleRequested; m_toggleRequested = false; return t;
    }

    void setInitialBodies(const std::vector<Body>& b, const FrameGraph& fg) {
        m_initialBodies = b;
        m_energy0       = Physics::totalEnergy(b, fg);
    }

private:
    std::vector<Body> m_initialBodies;
    double            m_energy0        = 0.0;
    bool              m_toggleRequested = false;
    unsigned          m_rng             = 22222u;

    // Parcours récursif : chaque contexte sous son vrai parent. Un affichage
    // linéaire trié par index rangerait la Lune sous Neptune.
    static void drawFrameTree(const FrameGraph& fg, int parent, int depth,
                              int highlight)
    {
        for (int f = 0; f < fg.size(); ++f) {
            if (fg.frames[f].parent != parent) continue;
            std::string indent(3 * (size_t)depth, ' ');
            const char* mark = (f == highlight) ? ">" : " ";
            if (fg.frames[f].soi > 0.0)
                ImGui::Text("%s%s %s  (SOI %.3e km)", indent.c_str(), mark,
                            fg.name(f).c_str(), fg.frames[f].soi);
            else
                ImGui::Text("%s%s %s", indent.c_str(), mark, fg.name(f).c_str());
            drawFrameTree(fg, f, depth + 1, highlight);
        }
    }

    // Écart entre deux doubles consécutifs à cette magnitude
    static double ulpOf(double magnitude) {
        return std::nextafter(magnitude, 1e308) - magnitude;
    }

    static std::string formatDistance(double km) {
        char buf[64];
        if (km >= Constants::AU_KM * 0.01)
            snprintf(buf, sizeof buf, "%.4f UA", km / Constants::AU_KM);
        else if (km >= 1.0)
            snprintf(buf, sizeof buf, "%.0f km", km);
        else
            snprintf(buf, sizeof buf, "%.1f m", km * 1000.0);
        return std::string(buf);
    }

    static std::string formatSpeed(double kms) {
        char buf[64];
        if (kms >= 1000.0)
            snprintf(buf, sizeof buf, "%.3e km/s", kms);
        else if (kms >= 1.0)
            snprintf(buf, sizeof buf, "%.2f km/s", kms);
        else
            snprintf(buf, sizeof buf, "%.0f m/s", kms * 1000.0);
        return std::string(buf);
    }

    static std::string formatTime(double seconds) {
        double days = seconds / Constants::DAY_S;
        long long y = static_cast<long long>(days / 365.25);
        double    d = days - double(y) * 365.25;
        char buf[64];
        snprintf(buf, sizeof buf, "%lld ans, %.2f j", y, d);
        return std::string(buf);
    }
};
