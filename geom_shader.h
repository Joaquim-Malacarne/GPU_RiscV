#ifndef GEOM_SHADER_H
#define GEOM_SHADER_H

#include <cstdint>
#include <vector>
#include "VRAM.h"

/*
 * Geometry shader — roda apenas no núcleo 0.
 *
 * Responsabilidades:
 *   1. Lê angle_idx da VRAM e busca sin/cos nas LUTs
 *   2. Aplica rotação Y e X em ponto fixo Q8.8 (escala 256) nos 8 vértices
 *   3. Projeta perspectiva → coordenadas de tela inteiras
 *   4. Rasteriza as 12 arestas via Bresenham → grava edge mask
 *   5. ECALL
 *
 * init_geometry_vram() deve ser chamada uma vez na inicialização para
 * escrever na VRAM: sin_lut, cos_lut e as coordenadas dos vértices do cubo.
 */

void init_geometry_vram(VRAM& vram, size_t N);

std::vector<uint32_t> build_geometry_shader(uint32_t N, uint32_t W, uint32_t H);

#endif // GEOM_SHADER_H
