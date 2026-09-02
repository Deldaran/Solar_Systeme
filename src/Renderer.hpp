#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Renderer.hpp — Pipeline OpenGL (SOLID: SRP)
//
//  Le seul endroit où l'on passe de float64 à float32. La règle : résoudre
//  la position d'un corps RELATIVEMENT à la caméra en double, en remontant
//  la chaîne de contextes (jamais via une coordonnée absolue), puis caster.
//  Le résultat est un petit nombre → le float32 est largement suffisant.
//
//  Les emplacements d'uniformes sont résolus une seule fois à l'init.
// ════════════════════════════════════════════════════════════════════════

#include "GL.hpp"
#include "Body.hpp"
#include "Frame.hpp"
#include "Camera.hpp"
#include "Frustum.hpp"
#include "Occlusion.hpp"
#include "Shaders.hpp"
#include "Constants.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <deque>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

class Renderer {
public:
    void init() {
        buildQuad();
        buildTrailBuffer();

        const std::string fs = Shaders::fragmentSource();
        m_prog = buildProgram(Shaders::VS, fs.c_str());
        cacheBodyLocations(m_prog, m_loc);
        m_uInvRot   = glGetUniformLocation(m_prog, "uInvRot");
        m_uHalfW    = glGetUniformLocation(m_prog, "uHalfW");
        m_uHalfH    = glGetUniformLocation(m_prog, "uHalfH");
        m_uCount    = glGetUniformLocation(m_prog, "uBodyCount");
        m_uExposure = glGetUniformLocation(m_prog, "uExposure");
        m_uScreenH  = glGetUniformLocation(m_prog, "uScreenH");

        m_trailProg = buildProgram(Shaders::TRAIL_VS, Shaders::TRAIL_FS);
        cacheBodyLocations(m_trailProg, m_tloc);
        m_tRot        = glGetUniformLocation(m_trailProg, "uRot");
        m_tHalfW      = glGetUniformLocation(m_trailProg, "uHalfW");
        m_tHalfH      = glGetUniformLocation(m_trailProg, "uHalfH");
        m_tCount      = glGetUniformLocation(m_trailProg, "uCount");
        m_tBodyCount  = glGetUniformLocation(m_trailProg, "uBodyCount");
        m_tTrailColor = glGetUniformLocation(m_trailProg, "uTrailColor");
    }

    // ── Passe corps ───────────────────────────────────────────────────
    void draw(const Camera& cam, const std::vector<Body>& bodies,
              const FrameGraph& fg, int fbW, int fbH, float exposure = 1.f)
    {
        if (fbW <= 0 || fbH <= 0) return;            // fenêtre minimisée
        m_aspect = double(fbW) / double(fbH);
        m_frustum.build(cam, m_aspect);

        // ── Résolution contextuelle → espace caméra-relatif (double) ──
        // On résout TOUS les corps, puis on choisit lesquels envoyer.
        m_rel.resize(bodies.size());
        for (size_t i = 0; i < bodies.size(); ++i)
            m_rel[i] = fg.separation(cam.pos, cam.frame,
                                     bodies[i].pos, bodies[i].frame, bodies);

        // ── Sélection ────────────────────────────────────────────────
        // Le shader ne connaît que les corps qu'on lui envoie : il y
        // cherche sa source de lumière ET ses occulteurs. Un corps retiré
        // du lot cesse donc d'exister — d'où les ombres qui disparaissent
        // quand leur projeteur sort du champ.
        m_keep.assign(bodies.size(), 0);

        // 1. Les étoiles, toujours : lumière + halo.
        for (size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].emissive > 0.5f) m_keep[i] = 1;

        // 2. Ce qui est effectivement à l'écran.
        for (size_t i = 0; i < bodies.size(); ++i)
            if (m_frustum.testSphere(m_rel[i], double(bodies[i].visualRadius())))
                m_keep[i] = 1;

        // 3. Ce qui projette une ombre sur un corps retenu, même hors champ.
        const size_t nVisible = bodies.size();
        for (size_t a = 0; a < nVisible; ++a) {
            if (!m_keep[a] || bodies[a].emissive > 0.5f) continue;
            for (size_t l = 0; l < nVisible; ++l) {
                if (bodies[l].emissive < 0.5f) continue;
                for (size_t b = 0; b < nVisible; ++b) {
                    if (b == a || m_keep[b] || bodies[b].emissive > 0.5f) continue;
                    if (Occlusion::castsShadow(m_rel[a], double(bodies[a].visualRadius()),
                                    m_rel[l],
                                    m_rel[b], double(bodies[b].visualRadius())))
                        m_keep[b] = 1;
                }
            }
        }

        m_visible.clear();
        m_relPos.clear();
        for (size_t i = 0; i < bodies.size(); ++i) {
            if (!m_keep[i]) continue;
            if ((int)m_visible.size() >= Constants::MAX_BODIES) break;
            m_visible.push_back(&bodies[i]);
            m_relPos.push_back(glm::vec3(m_rel[i]));  // cast float ICI, et ici seulement
        }
        m_count = (int)m_visible.size();

        double fovRad = glm::radians(double(cam.fov));
        m_halfH = float(std::tan(fovRad * 0.5));
        m_halfW = m_halfH * float(m_aspect);

        glUseProgram(m_prog);
        uploadBodies(m_loc);
        glUniform1i(m_uCount, m_count);
        glUniform1f(m_uExposure, exposure);
        glUniform1f(m_uHalfW, m_halfW);
        glUniform1f(m_uHalfH, m_halfH);
        glUniform1f(m_uScreenH, float(fbH));      // pilote le LOD d'octaves

        glm::mat4 invRot = cam.invRotationMatrix();
        glUniformMatrix4fv(m_uInvRot, 1, GL_FALSE, glm::value_ptr(invRot));

        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // ── Passe trail ───────────────────────────────────────────────────
    // Les points du trail sont stockés dans le contexte `trailFrame` :
    // l'orbite terrestre se referme donc exactement dans le repère du
    // Soleil, et celle de la Lune dans le repère de la Terre.
    // À appeler APRÈS draw() (réutilise la liste de corps visibles).
    void drawTrail(const Camera& cam, const std::deque<glm::dvec3>& trail,
                   int trailFrame, const FrameGraph& fg,
                   const std::vector<Body>& bodies, const glm::vec3& color)
    {
        if (trail.size() < 2 || !fg.valid(trailFrame)) return;

        const int n = std::min((int)trail.size(), TRAIL_CAPACITY);
        const int first = (int)trail.size() - n;

        // L'offset contexte→caméra est constant sur tout le trail : on le
        // calcule une fois, puis une simple soustraction par point.
        glm::dvec3 off = fg.originOffset(trailFrame, cam.frame, bodies) - cam.pos;

        m_trailVerts.clear();
        m_trailVerts.reserve(n * 3);
        for (int i = 0; i < n; ++i) {
            glm::vec3 p = glm::vec3(trail[first + i] + off);
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

    // Corps réellement transmis au shader (diagnostic)
    const std::vector<const Body*>& uploaded() const { return m_visible; }

    void cleanup() {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteVertexArrays(1, &m_trailVao);
        glDeleteBuffers(1, &m_trailVbo);
        glDeleteProgram(m_prog);
        glDeleteProgram(m_trailProg);
    }

    // Doit rester >= Simulation::TRAIL_MAX
    static constexpr int TRAIL_CAPACITY = 4000;

private:
    struct BodyLoc { GLint posRel, radius, color, emissive, surfType, surfSeed; };

    GLuint m_vao = 0, m_vbo = 0, m_prog = 0;
    GLuint m_trailVao = 0, m_trailVbo = 0, m_trailProg = 0;

    BodyLoc m_loc[Constants::MAX_BODIES];
    BodyLoc m_tloc[Constants::MAX_BODIES];
    GLint m_uInvRot = -1, m_uHalfW = -1, m_uHalfH = -1, m_uCount = -1,
          m_uExposure = -1, m_uScreenH = -1;
    GLint m_tRot = -1, m_tHalfW = -1, m_tHalfH = -1, m_tCount = -1,
          m_tBodyCount = -1, m_tTrailColor = -1;

    Frustum                  m_frustum;
    std::vector<glm::dvec3>  m_rel;
    std::vector<char>        m_keep;
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
            out[i].posRel   = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].radius", i);
            out[i].radius   = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].color", i);
            out[i].color    = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].emissive", i);
            out[i].emissive = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].surfType", i);
            out[i].surfType = glGetUniformLocation(prog, buf);
            snprintf(buf, sizeof buf, "uBodies[%d].surfSeed", i);
            out[i].surfSeed = glGetUniformLocation(prog, buf);
        }
    }

    void uploadBodies(const BodyLoc* loc) {
        for (int i = 0; i < m_count; ++i) {
            const Body& b = *m_visible[i];
            glUniform3fv(loc[i].posRel, 1, glm::value_ptr(m_relPos[i]));
            glUniform1f (loc[i].radius, b.visualRadius());
            glUniform3fv(loc[i].color, 1, glm::value_ptr(b.color));
            glUniform1f (loc[i].emissive, b.emissive);
            glUniform1f (loc[i].surfType, float(b.surfaceType));
            glUniform1f (loc[i].surfSeed, b.surfaceSeed);
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
                     (GLsizeiptr)(TRAIL_CAPACITY * 3 * sizeof(float)),
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
