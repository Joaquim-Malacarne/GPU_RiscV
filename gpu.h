#ifndef GPU_H
#define GPU_H

#include <cstdint>
#include <vector>
#include "VRAM.h"
#include "RV32ICore.h"

/*
 * GPUManager — pool de núcleos RV32I com dois buffers de VRAM.
 *
 * Fluxo por frame:
 *   1. set_angle(idx)       → CPU grava angle_idx no back_buffer
 *   2. load_geometry(prog)  → carrega geometry shader só no núcleo 0
 *   3. dispatch_geometry()  → executa núcleo 0 (rotação + projeção + Bresenham)
 *   4. load_program(prog)   → carrega fragment shader em todos os núcleos
 *   5. dispatch_frame()     → executa todos os núcleos em paralelo (coloração)
 *   6. get_pixels()         → retorna front_buffer para exibição
 *
 * Layout do back_buffer (tamanho = vl_total_size(N)):
 *   [0,  N)       → color buffer       (fragment shader escreve)
 *   [N, 2N)       → edge mask          (geometry shader escreve)
 *   [2N, 2N+553)  → dados de controle  (veja vram_layout.h)
 */
class GPUManager {
    size_t num_cores;
    size_t total_pixels;   // N
    int    num_threads;    // 0 = hardware_concurrency

    std::vector<RV32ICore> processing_units;
    VRAM back_buffer;
    VRAM front_buffer;

    static void run_core(RV32ICore* core);
    void swap_buffers();

public:
    // num_threads_hint: 0 = detectar automaticamente
    GPUManager(size_t cores_count, size_t pixels, int num_threads_hint = 0);

    // Carrega programa em todos os núcleos
    void load_program(const std::vector<uint32_t>& prog);

    // Carrega programa apenas no núcleo core_id
    void load_geometry(const std::vector<uint32_t>& prog, size_t core_id = 0);

    // Escreve angle_idx na posição 2N do back_buffer
    void set_angle(uint32_t angle_idx);

    // Despacha apenas o núcleo core_id (síncrono)
    void dispatch_geometry(size_t core_id = 0);

    // Despacha todos os núcleos em batches (paralelo)
    void dispatch_frame();

    // Retorna referência à VRAM (para inicialização externa)
    VRAM& vram() { return back_buffer; }

    // Retorna front_buffer pronto para exibição
    const std::vector<uint32_t>& get_pixels() const;
};

#endif // GPU_H
