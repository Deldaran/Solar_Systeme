#pragma once

// ════════════════════════════════════════════════════════════════════════
//  UI.hpp — Panneau ImGui (SOLID: SRP)
//  Responsabilité unique : afficher et modifier les paramètres.
// ════════════════════════════════════════════════════════════════════════

#include "imgui.h"
#include "Body.hpp"
#include "Camera.hpp"
#include "Simulation.hpp"
#include "Physics.hpp"
#include "Constants.hpp"

#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <cstdio>

class UI {
public:
    int   followBodyIndex = -1;   // -1 = caméra libre
    float exposure        = 1.f;

    // Retourne true si la caméra a été modifiée (besoin de updateFromOrbit)
    bool draw(Camera& cam, std::vector<Body>& bodies,
              Simulation& sim, float fps, float screenH, int drawnBodies)
    {
        bool camDirty = false;

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
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
                cam.target = glm::dvec3(0);
                camDirty   = true;
            }

            float dtD = float(sim.deltaT / Constants::DAY_S);
            if (ImGui::SliderFloat("DeltaT (jours/frame)", &dtD, 0.001f, 365.f,
                                   "%.3f", ImGuiSliderFlags_Logarithmic))
                sim.deltaT = double(dtD) * Constants::DAY_S;
            ImGui::SliderInt("Sous-etapes/frame", &sim.stepsPerFrame, 1, 400);

            // ── Garde-fou d'intégration ────────────────────────────────
            // Velocity Verlet reste stable tant que le pas est petit devant
            // le temps dynamique de l'orbite la plus serrée (ici la Lune).
            double sub  = sim.subStepSeconds();
            double tDyn = Physics::shortestDynamicalTime(bodies);
            double reco = tDyn / 20.0;
            ImGui::Text("Sous-pas : %.0f s", sub);
            ImGui::SameLine();
            if (reco > 0.0 && sub > reco)
                ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1),
                    "  INSTABLE (recommande < %.0f s)", reco);
            else
                ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "  stable");

            if (m_energy0 != 0.0) {
                double e    = Physics::totalEnergy(bodies);
                double drift = (e - m_energy0) / std::abs(m_energy0);
                ImGui::Text("Derive d'energie : %+.3e", drift);
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
            if (ImGui::SliderFloat("Distance (UA)", &distAU, 1e-6f, 60.f,
                                   "%.6f", ImGuiSliderFlags_Logarithmic)) {
                cam.distance = distAU * float(Constants::AU_KM);
                camDirty = true;
            }
            camDirty |= ImGui::SliderFloat("FOV", &cam.fov, 5.f, 120.f);
            ImGui::SliderFloat("Exposition", &exposure, 0.1f, 4.f);

            // ── Suivi ──────────────────────────────────────────────────
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
                        cam.target = bodies[i].pos;
                        // Se placer à une distance lisible du corps choisi
                        cam.distance = float(bodies[i].radius * bodies[i].visualScale * 8.0);
                        camDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();

        // ── Corps célestes ────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Corps celestes")) {
            ImGui::BeginChild("bodies", ImVec2(0, 240), true);
            for (int i = 0; i < (int)bodies.size(); ++i) {
                Body& b = bodies[i];
                ImGui::PushID(i);
                if (ImGui::TreeNode(b.name.c_str())) {
                    glm::dvec3 pAU = b.pos / Constants::AU_KM;
                    ImGui::Text("Pos (UA):  %.6f  %.6f  %.6f", pAU.x, pAU.y, pAU.z);
                    ImGui::Text("Vitesse:   %.3f km/s", glm::length(b.vel));
                    if (i != 0)
                        ImGui::Text("Dist Soleil: %.6f UA",
                            glm::length(b.pos - bodies[0].pos) / Constants::AU_KM);
                    ImGui::Text("Rayon reel: %.0f km", b.radius);
                    ImGui::SliderFloat("Visual Scale", &b.visualScale, 1.f, 2000.f,
                                       "%.1f", ImGuiSliderFlags_Logarithmic);
                    ImGui::ColorEdit3("Couleur", &b.color.x);
                    if (ImGui::Button("Tracer l'orbite")) {
                        sim.trailBody = i;
                        sim.trail.clear();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (ImGui::Button("Echelles reelles")) {
                for (auto& b : bodies) b.visualScale = 1.f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Grossir les planetes")) {
                for (auto& b : bodies)
                    if (b.emissive < 0.5f) b.visualScale = 800.f;
            }
        }
        ImGui::Separator();

        // ── Orbite ────────────────────────────────────────────────────
        ImGui::Checkbox("Afficher trail orbite", &sim.showTrail);
        if (sim.trailBody >= 0 && sim.trailBody < (int)bodies.size()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s, %d pts)",
                bodies[sim.trailBody].name.c_str(), (int)sim.trail.size());
        }
        ImGui::Separator();

        // ── Debug ─────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Debug technique")) {
            glm::dvec3 cp = cam.posWorld;
            ImGui::Text("Cam pos (km): %.6e  %.6e  %.6e", cp.x, cp.y, cp.z);
            glm::dvec3 t = cam.target;
            ImGui::Text("Target  (km): %.6e  %.6e  %.6e", t.x, t.y, t.z);
            ImGui::Text("Corps envoyes au GPU : %d / %d",
                        drawnBodies, (int)bodies.size());
            glm::dvec3 bc = Physics::barycenter(bodies);
            ImGui::Text("Barycentre : %.3e km (%.6f UA)",
                        glm::length(bc), glm::length(bc) / Constants::AU_KM);
            ImGui::Separator();
            ImGui::BulletText("Simulation : float64 (double)");
            ImGui::BulletText("Rendu : float32, camera a l'origine");
            ImGui::BulletText("Frustum Culling : double precision CPU");
            ImGui::BulletText("Ray casting analytique 100%% GPU");
        }

        ImGui::Separator();
        ImGui::Text("FPS : %.1f", fps);
        ImGui::End();

        // ── Barre d'aide ──────────────────────────────────────────────
        // screenH est en POINTS logiques ImGui, pas en pixels framebuffer :
        // sur un écran Retina les deux diffèrent d'un facteur 2 et la barre
        // partait hors de l'écran.
        ImGui::SetNextWindowPos(ImVec2(10, screenH - 44), ImGuiCond_Always);
        ImGui::Begin("##help", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        ImGui::TextDisabled("Clic G + glisser : orbite   |   Molette : zoom   |   Espace : pause");
        ImGui::End();

        return camDirty;
    }

    // Enregistre l'état initial pour le Reset
    void setInitialBodies(const std::vector<Body>& b) {
        m_initialBodies = b;
        m_energy0       = Physics::totalEnergy(b);
    }

private:
    std::vector<Body> m_initialBodies;
    double            m_energy0 = 0.0;

    static std::string formatTime(double seconds) {
        double days = seconds / Constants::DAY_S;
        long long y = static_cast<long long>(days / 365.25);
        double    d = days - double(y) * 365.25;
        std::ostringstream ss;
        char buf[64];
        snprintf(buf, sizeof buf, "%lld ans, %.2f j", y, d);
        ss << buf;
        return ss.str();
    }
};
