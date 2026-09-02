#pragma once

// ════════════════════════════════════════════════════════════════════════
//  UI.hpp — Panneau ImGui (SOLID: SRP)
//  Responsabilité unique : afficher et modifier les paramètres.
// ════════════════════════════════════════════════════════════════════════

#include "imgui.h"
#include "Body.hpp"
#include "Frame.hpp"
#include "Camera.hpp"
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
    int   followBodyIndex = -1;   // -1 = caméra libre
    float exposure        = 1.f;
    bool  autoContext     = true; // changement de contexte par sphère d'influence

    // Retourne true si la caméra a été modifiée (besoin de updateFromOrbit)
    bool draw(Camera& cam, std::vector<Body>& bodies, const FrameGraph& fg,
              Simulation& sim, float fps, float screenH, int drawnBodies)
    {
        bool camDirty = false;

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_Always);
        ImGui::Begin("Solar System", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Temps simule : %s",
            formatTime(sim.simTime).c_str());
        ImGui::Separator();

        // ── Simulation ────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Pause (Espace)", &sim.paused); ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                sim.reset(bodies, m_initialBodies);
                followBodyIndex = -1;
                cam.frame  = fg.size() > 1 ? 1 : 0;   // contexte héliocentrique
                cam.target = glm::dvec3(0);
                camDirty   = true;
            }

            float dtD = float(sim.deltaT / Constants::DAY_S);
            if (ImGui::SliderFloat("DeltaT (jours/frame)", &dtD, 0.001f, 365.f,
                                   "%.3f", ImGuiSliderFlags_Logarithmic))
                sim.deltaT = double(dtD) * Constants::DAY_S;
            ImGui::SliderInt("Sous-etapes/frame", &sim.stepsPerFrame, 1, 400);

            // Garde-fou : Verlet reste stable tant que le pas est petit
            // devant le temps dynamique de l'orbite la plus serrée.
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
                ImGui::Text("Derive d'energie : %+.3e", (e - m_energy0) / std::abs(m_energy0));
            }
        }
        ImGui::Separator();

        // ── Contexte (le cœur du modèle) ──────────────────────────────
        if (ImGui::CollapsingHeader("Contexte", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1, 1), "Camera dans : %s",
                fg.valid(cam.frame) ? fg.name(cam.frame).c_str() : "?");
            ImGui::Text("Position locale : %.3f  %.3f  %.3f km",
                        cam.pos.x, cam.pos.y, cam.pos.z);

            glm::dvec3 abs = fg.toRoot(cam.pos, cam.frame, bodies);
            ImGui::TextDisabled("Absolu (diagnostic) : %.4e km  (%.4f UA)",
                glm::length(abs), glm::length(abs) / Constants::AU_KM);

            // Comparaison de résolution : c'est tout l'argument du modèle.
            double locMag = std::max(1.0, glm::length(cam.pos));
            double absMag = std::max(1.0, glm::length(abs));
            ImGui::Text("Resolution float64 ici : %.2e km", ulpOf(locMag));
            ImGui::TextDisabled("   (en absolu ce serait %.2e km, soit %.0fx pire)",
                ulpOf(absMag), ulpOf(absMag) / ulpOf(locMag));

            ImGui::Checkbox("Changement de contexte auto (SOI)", &autoContext);
            if (ImGui::BeginCombo("Forcer le contexte",
                    fg.valid(cam.frame) ? fg.name(cam.frame).c_str() : "?")) {
                for (int f = 0; f < fg.size(); ++f) {
                    if (ImGui::Selectable(fg.name(f).c_str(), cam.frame == f)) {
                        // Translation exacte : la caméra ne bouge pas d'un mm
                        glm::dvec3 v(0.0);
                        int nf = cam.frame;
                        fg.reparent(cam.pos, v, nf, f, bodies);
                        cam.target = fg.convert(cam.target, cam.frame, f, bodies);
                        cam.frame  = f;
                        autoContext = false;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();

        // ── Caméra ────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            ImGui::TextDisabled("   = %.0f km", double(cam.distance));
            camDirty |= ImGui::SliderFloat("FOV", &cam.fov, 5.f, 120.f);
            ImGui::SliderFloat("Exposition", &exposure, 0.1f, 4.f);

            const char* current = (followBodyIndex >= 0 &&
                                   followBodyIndex < (int)bodies.size())
                                ? bodies[followBodyIndex].name.c_str()
                                : "(camera libre)";
            if (ImGui::BeginCombo("Suivi", current)) {
                if (ImGui::Selectable("(camera libre)", followBodyIndex < 0))
                    followBodyIndex = -1;
                for (int i = 0; i < (int)bodies.size(); ++i) {
                    if (ImGui::Selectable(bodies[i].name.c_str(), followBodyIndex == i)) {
                        followBodyIndex = i;
                        enterBodyContext(cam, fg, bodies, i);
                        cam.distance = float(bodies[i].radius *
                                             double(bodies[i].visualScale) * 8.0);
                        camDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();

        // ── Corps célestes ────────────────────────────────────────────
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
                    ImGui::Text("Vit. locale: %.4f km/s", glm::length(b.vel));
                    ImGui::TextDisabled("Vit. absolue: %.4f km/s",
                        glm::length(Physics::absoluteVel(b, fg, bodies)));
                    glm::dvec3 ab = Physics::absolutePos(b, fg, bodies);
                    ImGui::TextDisabled("Absolu : %.6f UA",
                        glm::length(ab) / Constants::AU_KM);
                    ImGui::Text("Rayon reel : %.0f km", b.radius);
                    int fr = fg.frameAnchoredTo(i);
                    if (fr >= 0 && fg.frames[fr].soi > 0.0)
                        ImGui::Text("SOI : %.4e km", fg.frames[fr].soi);
                    ImGui::SliderFloat("Visual Scale", &b.visualScale, 1.f, 2000.f,
                                       "%.1f", ImGuiSliderFlags_Logarithmic);
                    ImGui::ColorEdit3("Couleur", &b.color.x);
                    if (ImGui::Button("Tracer l'orbite")) sim.setTrailBody(i, bodies);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (ImGui::Button("Echelles reelles"))
                for (auto& b : bodies) b.visualScale = 1.f;
            ImGui::SameLine();
            if (ImGui::Button("Grossir les planetes"))
                for (auto& b : bodies)
                    if (b.emissive < 0.5f) b.visualScale = 800.f;
        }
        ImGui::Separator();

        // ── Orbite ────────────────────────────────────────────────────
        ImGui::Checkbox("Afficher trail orbite", &sim.showTrail);
        if (sim.trailBody >= 0 && sim.trailBody < (int)bodies.size()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s dans %s, %d pts)",
                bodies[sim.trailBody].name.c_str(),
                fg.valid(sim.trailFrame) ? fg.name(sim.trailFrame).c_str() : "?",
                (int)sim.trail.size());
        }
        ImGui::Separator();

        // ── Debug ─────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Debug technique")) {
            ImGui::Text("Corps envoyes au GPU : %d / %d", drawnBodies, (int)bodies.size());
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

        // Barre d'aide — screenH est en POINTS logiques ImGui, pas en
        // pixels framebuffer (les deux diffèrent d'un facteur 2 sur Retina).
        ImGui::SetNextWindowPos(ImVec2(10, screenH - 44), ImGuiCond_Always);
        ImGui::Begin("##help", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        ImGui::TextDisabled("Clic G + glisser : orbite   |   Molette : zoom   |   Espace : pause");
        ImGui::End();

        return camDirty;
    }

    // Entre dans le contexte porté par un corps (cible = origine exacte).
    static void enterBodyContext(Camera& cam, const FrameGraph& fg,
                                 const std::vector<Body>& bodies, int i)
    {
        int f = fg.frameAnchoredTo(i);
        if (f >= 0) {
            cam.frame  = f;
            cam.target = glm::dvec3(0.0);   // exactement zéro, pour toujours
        } else {
            cam.frame  = bodies[i].frame;
            cam.target = bodies[i].pos;
        }
    }

    void setInitialBodies(const std::vector<Body>& b, const FrameGraph& fg) {
        m_initialBodies = b;
        m_energy0       = Physics::totalEnergy(b, fg);
    }

private:
    std::vector<Body> m_initialBodies;
    double            m_energy0 = 0.0;

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

    static std::string formatTime(double seconds) {
        double days = seconds / Constants::DAY_S;
        long long y = static_cast<long long>(days / 365.25);
        double    d = days - double(y) * 365.25;
        char buf[64];
        snprintf(buf, sizeof buf, "%lld ans, %.2f j", y, d);
        return std::string(buf);
    }
};
