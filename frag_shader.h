#ifndef FRAG_SHADER_H
#define FRAG_SHADER_H

#include <cstdint>
#include <vector>

/*
 * Fragment shader — roda em todos os NUM_CORES núcleos em paralelo.
 *
 * Cada núcleo (mhartid = id) processa (total_pixels / num_cores) pixels:
 *   - lê edge mask na região [N + base_addr, N + base_addr + ppc)
 *   - escreve AZUL (0x0000FFFF) se mask != 0, PRETO (0) caso contrário
 *
 * Registradores:
 *   a0 (x10) = mhartid        (preenchido pelo construtor de RV32ICore)
 *   t0 (x5)  = pixels_per_core (ppc)
 *   t1 (x6)  = base_addr = mhartid × ppc
 *   t2 (x7)  = mask_base = N
 *   s0 (x8)  = cor azul = 0x0000FFFF
 *   t3 (x28) = contador i
 *   t4 (x29) = output_addr
 *   t5 (x30) = mask_addr
 *   t6 (x31) = valor da máscara lido
 */
std::vector<uint32_t> build_fragment_shader(uint32_t num_cores, uint32_t N);

#endif // FRAG_SHADER_H
