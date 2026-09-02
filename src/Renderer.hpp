#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Renderer.hpp — Pipeline OpenGL (SOLID: SRP)
//  Responsabilité unique : init GL, upload des uniformes, draw calls.
//
//  Les emplacements d'uniformes sont résolus UNE FOIS à l'init.
//  L'ancienne version faisait 4 snprintf + 4 glGetUniformLocation par corps
//  et par frame (lookup par chaîne dans le driver) : 128 lookups/frame à
//  32 corps.
// ════════════════════════════════════════════════════════════════════════

#include "GL.hpp"
#include "Body.hpp"
#include "Camera.hpp"
#include "Frustum.hpp"
#include "Shaders.hpp"
#include "Constants.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <deque>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

class Renderer {
public:
    // ── Init : compile les shaders, crée les buffers ──────────────────
    void init() {
        buildQuad();
        buildTrailBuffer();

        m_prog = buildProgram(Shaders::VS, Shaders::FS);
        cacheBodyLocations(m_prog, m_loc);
        m_uInvRot   = glGetUniformLocation(m_prog, "uInvRot");
        m_uHalfW    = glGetUniformLocation(m_prog, "uHalfW");
        m_uHalfH    = glGetUniformLocation(m_prog, "uHalfH");
        m_uCount    = glGetUniformLocation(m_prog, "uBodyCount");
        m_uExposure = glGetUniformLocation(m_prog, "uExposure");

        m_trailProg = buildProgram(Shaders::TRAIL_VS, Shaders::TRAIL_FS);
        cacheBodyLocations(m_trailProg, m_tloc);
        m_tRot        = glGetUniformLocation(m_trailProg, "uRot");
        m_tHalfW      = glGetUniformLocation(m_trailProg, "uHalfW");
        m_tHalfH      = glGetUniformLocation(m_trailProg, "uHalfH");
        m_tCount      = glGetUniformLocation(m_trailProg, "uCount");
        m_tBodyCount  = glGetUniformLocation(m_trailProg, "uBodyCount");
        m_tTrailColor = glGetUniformLocation(m_trailProg, "uTrailColor");
    }

    // ── Passe corps : ray casting plein écran ─────────────────────────
    void draw(const Camera& cam, const std::vector<Body>& bodies,
              int fbW, int fbH, float exposure = 1.f)
    {
        if (fbW <= 0 || fbH <= 0) return;            // fenêtre minimisée
        m_aspect = double(fbW) / double(fbH);

        m_frustum.build(cam, m_aspect);
        m_visible = m_frustum.cull(bodies);
        m_count   = std::min((int)m_visible.size(), Constants::MAX_BODIES);

        // Positions relatives à la caméra : soustraction en double, PUIS
        // cast en float — jamais l'inverse.
        m_relPos.resize(m_count);
        for (int i = 0; i < m_count; ++i)
            m_relPos[i] = glm::vec3(m_visible[i]->pos - cam.posWorld);

        double fovRad = glm::radians(double(cam.fov));
        m_halfH = float(std::tan(fovRad * 0.5));
        m_halfW = m_halfH * float(m_aspect);

        glUseProgram(m_prog);
        uploadBodies(m_loc);
        glUniform1i(m_uCount, m_count);
        glUniform1f(m_uExposure, exposure);
        glUniform1f(m_uHalfW, m_halfW);
        glUniform1f(m_uHalfH, m_halfH);

        glm::mat4 invRot = cam.invRotationMatrix();
        glUniformMatrix4fv(m_uInvRot, 1, GL_FALSE, glm::value_ptr(invRot));

        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // ── Passe trail : ligne d'orbite ──────────────────────────────────
    // À appeler APRÈS draw() (réutilise la liste de corps visibles).
    void drawTrail(const Camera& cam, const std::deque<glm::dvec3>& trail,
                   const glm::vec3& color)
    {
        if (trail.size() < 2) return;

        const int n = std::min((int)trail.size(), Simulation_TRAIL_MAX);
        m_trailVerts.clear();
        m_trailVerts.reserve(n * 3);
        // Les points les plus récents sont à la fin du deque.
        const int first = (int)trail.size() - n;
        for (int i = 0; i < n; ++i) {
            glm::vec3 p = glm::vec3(trail[first + i] - cam.posWorld);
            m_trailVerts.push_back(p.x);
            m_trailVerts.push_back(p.y);
            m_trailVerts.push_back(p.z);
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_trailVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(m_trailVerts.size() * sizeof(float)),
                        m_trailVerts.data());

        glUseProgram(m_trailProg);
        uploadBodies(m_tloc);
        glUniform1i(m_tBodyCount, m_count);
        glUniform1i(m_tCount, n);
        glUniform1f(m_tHalfW, m_halfW);
        glUniform1f(m_tHalfH, m_halfH);
        glUniform3fv(m_tTrailColor, 1, glm::value_ptr(color));

        glm::mat4 rot = cam.rotationMatrix();
        glUniformMatrix4fv(m_tRot, 1, GL_FALSE, glm::value_ptr(rot));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(m_trailVao);
        glDrawArrays(GL_LINE_STRIP, 0, n);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glUseProgram(0);
    }

    int visibleCount() const { return m_count; }

    void cleanup() {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteVertexArrays(1, &m_trailVao);
        glDeleteBuffers(1, &m_trailVbo);
        glDeleteProgram(m_prog);
        glDeleteProgram(m_trailProg);
    }

private:
    // Doit rester >= Simulation::TRAIL_MAX (évite d'inclure Simulation.hpp
    // ici, ce qui créerait une dépendance circulaire de responsabilités).
    static constexpr int Simulation_TRAIL_MAX = 3000;

    // ── Emplacements d'uniformes par corps, résolus une seule fois ────
    struct BodyLoc { GLint posRel, radius, color, emissive; };

    GLuint m_vao = 0, m_vbo = 0, m_prog = 0;
    GLuint m_trailVao = 0, m_trailVbo = 0, m_trailProg = 0;

    BodyLoc m_loc[Constants::MAX_BODIES];
    BodyLoc m_tloc[Constants::MAX_BODIES];
    GLint m_uInvRot = -1, m_uHalfW = -1, m_uHalfH = -1, m_uCount = -1, m_uExposure = -1;
    GLint m_tRot = -1, m_tHalfW = -1, m_tHalfH = -1, m_tCount = -1,
          m_tBodyCount = -1, m_tTrailColor = -1;

    Frustum                  m_frustum;
    std::vector<const Body*> m_visible;
    std::vector<glm::vec3>   m_relPos;
    std::vector<float>       m_trailVerts;
    int    m_count  = 0;
    double m_aspect = 1.0;
    float  m_halfW  = 1.f, m_halfH = 1.f;

    static void cacheBodyLocations(GLuint prog, BodyLoc* out) {
        char buf[64];
        for (int i = 0; i < Constants::MAX_BODIES; ++i) {
            snprintf(buf, sizeof buf, "uBodies[%d].posRel", i);
            out[i].posRel = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].radius", i);
            out[i].radius = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].color", i);
            out[i].color = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].emissive", i);
            out[i].emissive = glGetUniformLocation(prog, buf);
        }
    }

    void uploadBodies(const BodyLoc* loc) {
        for (int i = 0; i < m_count; ++i) {
            const Body& b = *m_visible[i];
            glUniform3fv(loc[i].posRel, 1, glm::value_ptr(m_relPos[i]));
            glUniform1f (loc[i].radius, b.visualRadius());
            glUniform3fv(loc[i].color, 1, glm::value_ptr(b.color));
            glUniform1f (loc[i].emissive, b.emissive);
        }
    }

    void buildQuad() {
        float verts[] = {
            -1.f,-1.f,  1.f,-1.f,  1.f, 1.f,
            -1.f,-1.f,  1.f, 1.f, -1.f, 1.f
        };
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }

    void buildTrailBuffer() {
        glGenVertexArrays(1, &m_trailVao);
        glGenBuffers(1, &m_trailVbo);
        glBindVertexArray(m_trailVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_trailVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(Simulation_TRAIL_MAX * 3 * sizeof(float)),
                     nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }

    static GLuint compileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[4096]; glGetShaderInfoLog(s, 4096, nullptr, buf);
            fprintf(stderr, "[SHADER ERROR]\n%s\n", buf);
            std::abort();   // un shader cassé = écran noir silencieux
        }
        return s;
    }

    static GLuint buildProgram(const char* vs, const char* fs) {
        GLuint p  = glCreateProgram();
        GLuint s0 = compileShader(GL_VERTEX_SHADER,   vs);
        GLuint s1 = compileShader(GL_FRAGMENT_SHADER, fs);
        glAttachShader(p, s0); glAttachShader(p, s1);
        glLinkProgram(p);
        GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[1024]; glGetProgramInfoLog(p, 1024, nullptr, buf);
            fprintf(stderr, "[LINK ERROR]\n%s\n", buf);
            std::abort();
        }
        glDeleteShader(s0); glDeleteShader(s1);
        return p;
    }
};
