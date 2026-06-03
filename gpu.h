#ifndef GPU_H
#define GPU_H

#include <cstdint>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "config.h"
#include "VRAM.h"
#include "RV32ICore.h"
#include "shared_frame.h"

/*
 * GPUManager — COMPUTE_THREADS threads em spin loop contínuo.
 *
 * Fluxo por frame (3 fases sincronizadas por spin barriers):
 *   Fase 0  — thread líder (t=0): grava ângulo, executa geometry shader no núcleo 0,
 *             troca instruction_memory do núcleo 0 para fragment.
 *   Fase 1  — todas as 7 threads: reset + execute nos núcleos atribuídos (100% CPU).
 *   Fase 2  — thread líder: swap de buffers, publica em SharedFrame,
 *             restaura geometry no núcleo 0 para a próxima iteração.
 *
 * Distribuição de núcleos entre 7 threads (stride = COMPUTE_THREADS):
 *   t0 → cores 0, 7, 14   t1 → cores 1, 8, 15   t2 → cores 2, 9
 *   t3 → cores 3, 10       t4 → cores 4, 11       t5 → cores 5, 12
 *   t6 → cores 6, 13
 */
class GPUManager {
    size_t num_cores;
    size_t total_pixels;

    std::vector<RV32ICore> processing_units;
    VRAM                   back_buffer;
    VRAM                   front_buffer;

    std::vector<uint32_t>  geom_prog_;
    std::vector<uint32_t>  frag_prog_;

    // ── Spin barrier ─────────────────────────────────────────────────────
    struct SpinBarrier {
        const int        n;
        std::atomic<int> count{0};
        std::atomic<int> gen{0};
        explicit SpinBarrier(int n_) : n(n_) {}
        void arrive_and_wait();
    };

    // 3 barreiras separadas: uma por fase (evita risco de reutilização prematura)
    SpinBarrier barrier_geom_;  // sincroniza após geometry
    SpinBarrier barrier_frag_;  // sincroniza após fragment
    SpinBarrier barrier_pub_;   // sincroniza após publicação (antes da prox. iter.)

    std::atomic<bool>     running_{false};
    std::atomic<uint32_t> angle_idx_{0};
    std::vector<std::thread> compute_threads_;

    void swap_buffers();
    void worker(int t, SharedFrame& shared);

public:
    GPUManager(size_t cores, size_t pixels);
    ~GPUManager();

    VRAM& vram() { return back_buffer; }

    // Armazena programas compilados e pré-carrega nos núcleos (chamado uma vez).
    void set_programs(const std::vector<uint32_t>& geom,
                      const std::vector<uint32_t>& frag);

    // Lança COMPUTE_THREADS threads em spin loop e bloqueia até shared.stop.
    void run_continuous(SharedFrame& shared);
};

#endif // GPU_H
