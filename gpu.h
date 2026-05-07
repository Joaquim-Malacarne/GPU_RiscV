#ifndef GPU_H
#define GPU_H

#include <cstdint>
#include <vector>
#include "VRAM.h"
#include "RV32ICore.h"

/*
 * GPUManager — gerencia núcleos RV32I e dois buffers de VRAM.
 *
 * Threads:
 *   Thread de controle  → chama dispatch_frame(); coordena os workers e faz o swap.
 *   Threads de núcleo   → cada uma executa run_core() para um único RV32ICore.
 *
 * Layout da VRAM (tamanho = 2 × total_pixels):
 *   back_buffer  [0 .. total)   → núcleos escrevem a cor aqui
 *   back_buffer  [total .. 2×total) → CPU grava a máscara de aresta aqui
 *   front_buffer [0 .. total)   → frame exibido ao usuário após o swap
 */
class GPUManager {
    size_t num_cores;
    size_t total_pixels;
    std::vector<RV32ICore> processing_units;
    VRAM back_buffer;
    VRAM front_buffer;

    // Executada por cada thread de núcleo
    static void run_core(RV32ICore* core);

    void swap_buffers();

public:
    GPUManager(size_t cores_count, size_t pixels);

    // Carrega o mesmo programa binário em todos os núcleos (reseta registradores e PC)
    void load_program(const std::vector<uint32_t>& prog);

    // Grava a máscara de aresta na segunda metade do back_buffer antes do dispatch
    void set_mask(const std::vector<uint32_t>& mask);

    // Thread de controle: lança as threads de núcleo em batches e faz o swap
    void dispatch_frame();

    // Retorna o front_buffer (frame completo pronto para exibição)
    const std::vector<uint32_t>& get_pixels() const;
};

#endif // GPU_H
