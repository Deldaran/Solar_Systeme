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

// ─────────────────────────────────────────────────────────────────────
//  Zoom : la distance minimale dépend du corps suivi, pas du Soleil.
// ─────────────────────────────────────────────────────────────────────
static float minZoomDistance()
{
    int fi = g_ui.followBodyIndex;
    if (fi >= 0 && fi < (int)g_sys.bodies.size()) {
        const Body& b = g_sys.bodies[fi];
        return float(b.radius * double(b.visualScale) * 1.15);
    }
    return float(Constants::RADIUS_SUN * 1.05);
}

static void clampZoom()
{
    g_camera.distance = glm::clamp(g_camera.distance,
        minZoomDistance(), float(Constants::AU_KM * 60.0));
}

// ─────────────────────────────────────────────────────────────────────
static void initScene()
{
    g_sys = BodyFactory::makeSolarSystem();

    // À 3 UA, une planète à l'échelle réelle fait une fraction de pixel.
    // L'utilisateur peut revenir aux échelles réelles depuis l'UI.
    for (auto& b : g_sys.bodies)
        if (b.emissive < 0.5f) b.visualScale = 800.f;

    g_camera.frame    = g_sys.sunFrame;    // contexte héliocentrique
    g_camera.target   = glm::dvec3(0);
    g_camera.theta    = 0.f;
    g_camera.phi      = 0.45f;
    g_camera.distance = float(Constants::AU_KM * 3.0);
    g_camera.fov      = 60.f;
    g_camera.updateFromOrbit();

    g_sim.simTime = 0.0;
    g_sim.invalidate();

    // Trace l'orbite de la Terre (index 3 : Soleil, Mercure, Venus, Terre)
    g_sim.setTrailBody(3, g_sys.bodies);

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
        float dx = float(mx - g_lastMx) * 0.005f;
        float dy = float(my - g_lastMy) * 0.005f;
        g_camera.theta -= dx;
        g_camera.phi = glm::clamp(g_camera.phi - dy,
            -glm::half_pi<float>() + 0.05f,
             glm::half_pi<float>() - 0.05f);
        g_camera.updateFromOrbit();
    }
    g_lastMx = mx; g_lastMy = my;
}

static void scrollCB(GLFWwindow*, double, double dy)
{
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_camera.distance *= (dy > 0) ? 0.85f : 1.15f;
    clampZoom();
    g_camera.updateFromOrbit();
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
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    g_renderer.init();
    initScene();

    // ── Boucle principale ────────────────────────────────────────────
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        g_sim.step(g_sys.bodies, g_sys.frames);

        int fi = g_ui.followBodyIndex;
        if (fi >= 0 && fi < (int)g_sys.bodies.size()) {
            // Suivre = vivre dans le contexte du corps. La cible reste
            // exactement (0,0,0) : rien à recalculer, aucune dérive.
            clampZoom();
            g_camera.updateFromOrbit();
        }
        else if (g_ui.autoContext) {
            // Caméra libre : on bascule vers le contexte le plus profond
            // dont la sphère d'influence nous contient. La conversion est
            // une translation exacte — aucune discontinuité à l'écran.
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // DisplaySize est en points logiques — sur Retina fbH vaut le double.
        bool camDirty = g_ui.draw(g_camera, g_sys.bodies, g_sys.frames, g_sim,
                                  io.Framerate, io.DisplaySize.y,
                                  g_renderer.visibleCount());
        if (camDirty) { clampZoom(); g_camera.updateFromOrbit(); }

        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.WantCaptureKeyboard)
            g_sim.paused = !g_sim.paused;

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
