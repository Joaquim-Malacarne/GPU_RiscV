#ifndef PROG_H
#define PROG_H

#include <cstdint>
#include <vector>

/*
 * Shader: lê máscara de aresta e pinta azul ou preto.
 *
 * Layout da VRAM (tamanho = 2 × total_pixels):
 *   [0 .. total_pixels)            → buffer de cor  (núcleos escrevem)
 *   [total_pixels .. 2×total)      → máscara de aresta (CPU escreve antes do dispatch)
 *
 * Mapa de registradores:
 *   x10 (a0) = mhartid               ← preenchido pelo construtor RV32ICore
 *   x5  (t0) = pixels_per_core
 *   x6  (t1) = base_addr = mhartid × ppc
 *   x7  (t2) = mask_base = total_pixels
 *   x8  (s0) = azul = 0x0000FFFF
 *   x28 (t3) = i  (contador)
 *   x29 (t4) = output_addr
 *   x30 (t5) = mask_addr
 *   x31 (t6) = valor da máscara
 */
std::vector<uint32_t> build_cube_shader(uint32_t num_cores, uint32_t total_pixels);

#endif // PROG_H
