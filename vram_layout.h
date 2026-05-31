#ifndef VRAM_LAYOUT_H
#define VRAM_LAYOUT_H

#include <cstddef>

/*
 * Layout linear da VRAM (endereços = índices de uint32_t):
 *
 *  [0,          N)          color buffer   — fragment shader escreve RGBA
 *  [N,         2N)          edge mask      — geometry shader escreve 0/1
 *  [2N + 0]                 angle_idx      — CPU escreve 1× por frame [0..255]
 *  [2N + 1,  2N+257)        sin_lut[256]   — ponto fixo Q8.8 (×256), init 1×
 *  [2N + 257,2N+513)        cos_lut[256]   — ponto fixo Q8.8 (×256), init 1×
 *  [2N + 513,2N+537)        cube_verts[24] — 8 vértices × 3 coords, Q8.8
 *  [2N + 537,2N+553)        screen_verts[16]— 8 vértices × 2 coords, pixels int
 *
 *  Tamanho total: 2N + 553 words
 */

inline size_t vl_color_base  (size_t /*N*/) { return 0; }
inline size_t vl_mask_base   (size_t N)     { return N; }
inline size_t vl_angle_addr  (size_t N)     { return 2*N + 0; }
inline size_t vl_sin_base    (size_t N)     { return 2*N + 1; }
inline size_t vl_cos_base    (size_t N)     { return 2*N + 257; }
inline size_t vl_verts_base  (size_t N)     { return 2*N + 513; }
inline size_t vl_screen_base (size_t N)     { return 2*N + 537; }
inline size_t vl_total_size  (size_t N)     { return 2*N + 553; }

// Escala de ponto fixo Q8.8 usada em toda a pipeline GPU
constexpr int FP_SCALE = 256;

#endif // VRAM_LAYOUT_H
