#pragma once

// ════════════════════════════════════════════════════════════════════════
//  Screenshot.hpp — Capture du framebuffer (SOLID: SRP)
//
//  Écrit un BMP 24 bits non compressé : format trivial, aucune dépendance,
//  et lisible par tous les visionneuses (et par `sips` sur macOS pour
//  convertir en PNG). Sert à valider le rendu procédural sans avoir à
//  regarder la fenêtre — et à produire des images de référence.
// ════════════════════════════════════════════════════════════════════════

#include "GL.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>

namespace Screenshot {

inline bool capture(const char* path, int w, int h)
{
    if (w <= 0 || h <= 0) return false;

    // Les lignes BMP sont alignées sur 4 octets, comme le pack GL par défaut.
    const int rowBytes = (w * 3 + 3) & ~3;
    std::vector<unsigned char> pix((size_t)rowBytes * h, 0);

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pix.data());

    // BMP stocke les lignes de bas en haut — comme OpenGL — et les pixels
    // en BGR. Une seule permutation suffit donc, pas de retournement.
    for (int y = 0; y < h; ++y) {
        unsigned char* row = pix.data() + (size_t)y * rowBytes;
        for (int x = 0; x < w; ++x) {
            unsigned char t = row[x * 3 + 0];
            row[x * 3 + 0]  = row[x * 3 + 2];
            row[x * 3 + 2]  = t;
        }
    }

    const uint32_t dataSize = (uint32_t)rowBytes * (uint32_t)h;
    const uint32_t offset   = 14 + 40;
    const uint32_t fileSize = offset + dataSize;

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "[ERREUR] ecriture impossible : %s\n", path); return false; }

    auto u16 = [&](uint16_t v) { std::fputc(v & 0xff, f); std::fputc((v >> 8) & 0xff, f); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc((v >> (8*i)) & 0xff, f); };

    // En-tête de fichier
    std::fputc('B', f); std::fputc('M', f);
    u32(fileSize); u16(0); u16(0); u32(offset);
    // En-tête d'image (BITMAPINFOHEADER)
    u32(40); u32((uint32_t)w); u32((uint32_t)h);
    u16(1); u16(24); u32(0); u32(dataSize);
    u32(2835); u32(2835); u32(0); u32(0);

    std::fwrite(pix.data(), 1, dataSize, f);
    std::fclose(f);
    return true;
}

} // namespace Screenshot
