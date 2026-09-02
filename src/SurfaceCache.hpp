#pragma once

// ════════════════════════════════════════════════════════════════════════
//  SurfaceCache.hpp — Précalcul des surfaces procédurales (SOLID: SRP)
//
//  Le problème : évaluer le bruit par fragment coûte ~400 hachages en gros
//  plan (fBm 10 octaves + Worley + déformation de domaine + relief). À
//  pleine résolution, c'est 30 ms par image pour une seule planète — et
//  se poser au sol, où la planète occupe tout l'écran, serait injouable.
//
//  La solution : cuire une fois la surface dans une texture, puis la lire.
//  Une lecture de texture remplace les 400 hachages.
//
//  ── Pourquoi six faces de cube, et non une carte équirectangulaire ────
//  Une carte lat/lon a deux défauts rédhibitoires ici : elle écrase toute
//  une ligne de texels sur chaque pôle (densité de détail absurde là-bas,
//  insuffisante à l'équateur), et elle a une couture en longitude. Les six
//  faces d'un cube ont une densité de texels quasi uniforme et aucune
//  singularité. Elles tiennent dans UN tableau de textures 2D — donc un
//  seul échantillonneur pour tous les corps, ce qui compte : GLSL 3.3 ne
//  garantit que 16 unités de texture.
//
//  ── Ce que le cache ne contient pas ──────────────────────────────────
//  Les fréquences plus fines que sa résolution. Elles restent calculées à
//  la volée, mais seulement quand le corps est gros à l'écran, et sur
//  quelques octaves. C'est ce qui permettra de descendre au sol sans
//  effondrement du framerate.
// ════════════════════════════════════════════════════════════════════════

#include "GL.hpp"
#include "Body.hpp"
#include "Shaders.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

class SurfaceCache {
public:
    static constexpr int FACES = 6;

    // `faceSize` : résolution d'une face. 256 donne 0.35° par texel, ce qui
    // suffit très largement tant qu'on n'est pas au ras du sol.
    void init(int faceSize = 512) {
        m_size = faceSize;

        m_prog = buildProgram(Shaders::BAKE_VS, Shaders::bakeFragmentSource().c_str());
        m_uFace     = glGetUniformLocation(m_prog, "uFace");
        m_uSurfType = glGetUniformLocation(m_prog, "uSurfType");
        m_uSurfSeed = glGetUniformLocation(m_prog, "uSurfSeed");
        m_uTint     = glGetUniformLocation(m_prog, "uTint");
        m_uOctaves  = glGetUniformLocation(m_prog, "uOctaves");

        glGenFramebuffers(1, &m_fbo);

        // Quad plein écran propre à la cuisson
        const float verts[] = {
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

    // (Re)cuit tous les corps. Appeler à l'init et après toute modification
    // de catégorie, de graine ou de teinte.
    void bake(const std::vector<Body>& bodies) {
        const int n = (int)bodies.size();
        if (n <= 0) return;

        if (m_layers != n * FACES) {
            if (m_tex) glDeleteTextures(1, &m_tex);
            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                         m_size, m_size, n * FACES, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            // Mipmaps : sans elles, une planète réduite à quelques pixels
            // ré-alias la carte à chaque image.
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // Bord serré : sans cela, le filtrage linéaire irait chercher
            // des texels de l'autre côté de la face et créerait des coutures.
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            m_layers = n * FACES;
        }

        GLint prevFbo = 0;   // on rendra ensuite dans le framebuffer par défaut
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_size, m_size);
        glUseProgram(m_prog);
        glBindVertexArray(m_vao);
        // ── Octaves accordées à la résolution ────────────────────────
        // Cuire plus d'octaves que la carte ne peut en résoudre revient à
        // échantillonner au-dessus de sa fréquence de Nyquist : la texture
        // elle-même sort aliasée, et le rendu affiche un moiré d'anneaux
        // concentriques. Une face de 2^k texels porte au plus k-2 octaves.
        int oct = 0; for (int t = m_size; t > 1; t >>= 1) ++oct;   // log2
        oct = std::max(3, oct - 2);
        glUniform1i(m_uOctaves, oct);
        m_octaves = oct;

        for (int i = 0; i < n; ++i) {
            const Body& b = bodies[i];
            glUniform1f (m_uSurfType, float(b.surfaceType));
            glUniform1f (m_uSurfSeed, b.surfaceSeed);
            glUniform3fv(m_uTint, 1, glm::value_ptr(b.color));

            for (int f = 0; f < FACES; ++f) {
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          m_tex, 0, i * FACES + f);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    fprintf(stderr, "[CACHE] framebuffer incomplet\n");
                    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
                    return;
                }
                glUniform1i(m_uFace, f);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }

        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);

        glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        m_ready = true;
    }

    // Lie le cache sur l'unité de texture `unit`.
    void bind(int unit) const {
        glActiveTexture(GLenum(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex);
    }

    bool ready()    const { return m_ready; }
    int  faceSize() const { return m_size; }
    int  layers()   const { return m_layers; }
    int  octaves()  const { return m_octaves; }

    // Mémoire occupée, en octets (RGBA8)
    size_t bytes() const {
        return (size_t)m_size * m_size * 4 * (size_t)m_layers;
    }

    void cleanup() {
        if (m_tex) glDeleteTextures(1, &m_tex);
        if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_prog) glDeleteProgram(m_prog);
        m_tex = m_fbo = m_vao = m_vbo = m_prog = 0;
        m_ready = false;
    }

private:
    GLuint m_tex = 0, m_fbo = 0, m_vao = 0, m_vbo = 0, m_prog = 0;
    GLint  m_uFace = -1, m_uSurfType = -1, m_uSurfSeed = -1,
           m_uTint = -1, m_uOctaves = -1;
    int    m_size = 512, m_layers = 0, m_octaves = 0;
    bool   m_ready = false;

    static GLuint compileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[4096]; glGetShaderInfoLog(s, 4096, nullptr, buf);
            fprintf(stderr, "[BAKE SHADER ERROR]\n%s\n", buf);
            std::abort();
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
            fprintf(stderr, "[BAKE LINK ERROR]\n%s\n", buf);
            std::abort();
        }
        glDeleteShader(s0); glDeleteShader(s1);
        return p;
    }
};
