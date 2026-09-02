// ════════════════════════════════════════════════════════════════════════
//  main.cpp — Point d'entrée (SOLID: orchestration uniquement)
//  Init GLFW/ImGui, boucle principale, délégation aux sous-systèmes.
// ════════════════════════════════════════════════════════════════════════

#include "GL.hpp"            // avant ImGui (définit GLFW_INCLUDE_NONE)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <glm/gtc/constants.hpp>
#include <cstdio>

#include "Constants.hpp"
#include "Body.hpp"
#include "Frame.hpp"
#include "BodyFactory.hpp"
#include "Camera.hpp"
#include "Navigation.hpp"
#include "VisualScale.hpp"
#include "Simulation.hpp"
#include "Renderer.hpp"
#include "UI.hpp"

// ─────────────────────────────────────────────────────────────────────
//  État global
// ─────────────────────────────────────────────────────────────────────
static BodyFactory::System g_sys;      // corps + arbre de contextes
static Camera              g_camera;
static Simulation          g_sim;
static Renderer            g_renderer;
static UI                  g_ui;

static bool   g_mouseDrag = false;
static double g_lastMx = 0, g_lastMy = 0;

static void clampZoom()
{
    float mn = Navigation::minZoomDistance(g_sys.bodies, g_ui.followBodyIndex,
                                           Constants::RADIUS_SUN);
    g_camera.distance = glm::clamp(g_camera.distance, mn,
                                   float(Constants::AU_KM * 60.0));
}

static void applyVisualScale()
{
    VisualScale::apply(g_sys.bodies, g_sys.frames, g_ui.scaleMode, g_ui.scaleK);
}

// ─────────────────────────────────────────────────────────────────────
static void initScene()
{
    g_sys = BodyFactory::makeSolarSystem();
    applyVisualScale();

    g_camera.mode     = Camera::Mode::Orbit;
    g_camera.frame    = g_sys.sunFrame;    // contexte héliocentrique
    g_camera.target   = glm::dvec3(0);
    g_camera.theta    = 0.f;
    g_camera.phi      = 0.45f;
    g_camera.distance = float(Constants::AU_KM * 3.0);
    g_camera.fov      = 60.f;
    g_camera.updateFromOrbit();

    g_sim.simTime = 0.0;
    g_sim.invalidate();
    g_sim.setTrailBody(3, g_sys.bodies);   // la Terre

    g_ui.followBodyIndex = -1;
    g_ui.setInitialBodies(g_sys.bodies, g_sys.frames);
}

// ─────────────────────────────────────────────────────────────────────
//  Callbacks GLFW
// ─────────────────────────────────────────────────────────────────────
static void mouseButtonCB(GLFWwindow*, int btn, int action, int)
{
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (btn == GLFW_MOUSE_BUTTON_LEFT)
        g_mouseDrag = (action == GLFW_PRESS);
}

static void cursorPosCB(GLFWwindow*, double mx, double my)
{
    if (g_mouseDrag && !ImGui::GetIO().WantCaptureMouse) {
        float dx = float(mx - g_lastMx);
        float dy = float(my - g_lastMy);

        if (g_camera.mode == Camera::Mode::Free) {
            // Convention FPS : souris vers le bas = on regarde vers le bas.
            const float s = 0.003f;
            g_camera.yaw   -= dx * s;
            g_camera.pitch -= dy * s;
            g_camera.pitch = glm::clamp(g_camera.pitch,
                -glm::half_pi<float>() + 0.01f,
                 glm::half_pi<float>() - 0.01f);
            g_camera.updateFreeOrientation();
        } else {
            const float s = 0.005f;
            g_camera.theta -= dx * s;
            g_camera.phi = glm::clamp(g_camera.phi - dy * s,
                -glm::half_pi<float>() + 0.05f,
                 glm::half_pi<float>() - 0.05f);
            g_camera.updateFromOrbit();
        }
    }
    g_lastMx = mx; g_lastMy = my;
}

static void scrollCB(GLFWwindow*, double, double dy)
{
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (g_camera.mode == Camera::Mode::Free) {
        // En vol libre la molette règle le multiplicateur de vitesse.
        g_camera.speedBoost *= (dy > 0) ? 1.25f : 0.8f;
        g_camera.speedBoost = glm::clamp(g_camera.speedBoost, 0.01f, 1000.f);
    } else {
        g_camera.distance *= (dy > 0) ? 0.85f : 1.15f;
        clampZoom();
        g_camera.updateFromOrbit();
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Bascule Orbite <-> Libre (touche V)
// ─────────────────────────────────────────────────────────────────────
static void toggleCameraMode()
{
    if (g_camera.mode == Camera::Mode::Orbit) {
        // Vers le vol libre : on lâche le suivi et on laisse les sphères
        // d'influence gérer le contexte au fil du déplacement.
        g_camera.toFree();
        g_ui.followBodyIndex = -1;
        g_ui.autoContext     = true;
    } else {
        // Retour en orbite autour du corps le plus proche : on entre dans
        // son contexte et il devient le centre exact de l'écran.
        auto n = Navigation::nearestBody(g_camera, g_sys.bodies, g_sys.frames);
        if (n.index >= 0) {
            Navigation::focusOn(g_camera, g_sys.frames, g_sys.bodies, n.index);
            g_ui.followBodyIndex = n.index;
            clampZoom();
            g_camera.updateFromOrbit();
        } else {
            g_camera.toOrbit(g_camera.target);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
//  Vol libre : WASD + montée/descente, vitesse indexée sur l'altitude
// ─────────────────────────────────────────────────────────────────────
static void updateFreeFlight(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    glm::dvec3 axis(0.0);
    if (ImGui::IsKeyDown(ImGuiKey_W)) axis.z += 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_S)) axis.z -= 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_D)) axis.x += 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_A)) axis.x -= 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_Space)) axis.y += 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_C))     axis.y -= 1.0;

    if (glm::length(axis) < 1e-9) return;
    axis = glm::normalize(axis);

    auto   n = Navigation::nearestBody(g_camera, g_sys.bodies, g_sys.frames);
    double v = Navigation::freeFlySpeed(n.altitude, g_camera.speedBoost);

    if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) v *= 5.0;   // turbo
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))  v *= 0.2;   // précision

    g_camera.moveFree(axis * v * double(dt));
}

// ─────────────────────────────────────────────────────────────────────
int main()
{
    if (!glfwInit()) { fprintf(stderr, "[ERREUR] glfwInit a echoue\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        1440, 900, "Solar System — contextes float64 / rendu float32",
        nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "[ERREUR] Impossible de creer un contexte OpenGL 3.3\n");
        glfwTerminate(); return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!glLoadFunctions()) {
        fprintf(stderr, "[ERREUR] Chargement des fonctions OpenGL 3.3 echoue\n");
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }

    glfwSetMouseButtonCallback(window, mouseButtonCB);
    glfwSetCursorPosCallback(window,   cursorPosCB);
    glfwSetScrollCallback(window,      scrollCB);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    g_renderer.init();
    initScene();

    // ── Boucle principale ────────────────────────────────────────────
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGuiIO& io = ImGui::GetIO();

        g_sim.step(g_sys.bodies, g_sys.frames);

        // ── Caméra ───────────────────────────────────────────────────
        if (g_camera.mode == Camera::Mode::Free) {
            updateFreeFlight(io.DeltaTime);
        }
        else if (g_ui.followBodyIndex >= 0 &&
                 g_ui.followBodyIndex < (int)g_sys.bodies.size()) {
            // Suivre = vivre dans le contexte du corps. La cible reste
            // exactement (0,0,0) : rien à recalculer, aucune dérive.
            clampZoom();
            g_camera.updateFromOrbit();
        }

        // Bascule automatique de contexte par sphère d'influence.
        // Translation exacte : rien ne saute à l'écran.
        if (g_ui.autoContext && g_ui.followBodyIndex < 0) {
            int nf = g_sys.frames.bestFrameFor(g_camera.pos, g_camera.frame,
                                               g_sys.bodies);
            if (nf != g_camera.frame) {
                glm::dvec3 v(0.0);
                int cf = g_camera.frame;
                g_camera.target = g_sys.frames.convert(g_camera.target,
                                      g_camera.frame, nf, g_sys.bodies);
                g_sys.frames.reparent(g_camera.pos, v, cf, nf, g_sys.bodies);
                g_camera.frame = nf;
            }
        }

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);

        if (fbW > 0 && fbH > 0) {
            glViewport(0, 0, fbW, fbH);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);

            g_renderer.draw(g_camera, g_sys.bodies, g_sys.frames,
                            fbW, fbH, g_ui.exposure);
            if (g_sim.showTrail)
                g_renderer.drawTrail(g_camera, g_sim.trail, g_sim.trailFrame,
                                     g_sys.frames, g_sys.bodies,
                                     glm::vec3(0.35f, 0.75f, 1.f));
        }

        // ── ImGui ────────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto nearInfo = Navigation::nearestBody(g_camera, g_sys.bodies, g_sys.frames);

        // DisplaySize est en points logiques — sur Retina fbH vaut le double.
        bool camDirty = g_ui.draw(g_camera, g_sys.bodies, g_sys.frames, g_sim,
                                  io.Framerate, io.DisplaySize.y,
                                  g_renderer.visibleCount(), nearInfo);
        if (g_ui.scaleDirty) { applyVisualScale(); g_ui.scaleDirty = false; camDirty = true; }
        if (camDirty) {
            if (g_camera.mode == Camera::Mode::Orbit) { clampZoom(); g_camera.updateFromOrbit(); }
            else g_camera.updateFreeOrientation();
        }

        // ── Raccourcis clavier ───────────────────────────────────────
        if (g_ui.consumeToggleRequest()) toggleCameraMode();
        if (!io.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) toggleCameraMode();
            // Espace = pause en mode orbite ; en vol libre il sert à monter.
            if (g_camera.mode == Camera::Mode::Orbit &&
                ImGui::IsKeyPressed(ImGuiKey_Space, false))
                g_sim.paused = !g_sim.paused;
            if (ImGui::IsKeyPressed(ImGuiKey_P, false))
                g_sim.paused = !g_sim.paused;
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    g_renderer.cleanup();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
