/*
 * GPU-V  —  Emulador de GPU baseado em RISC-V RV32I+M
 * Universidade Católica de Santos
 *
 * Modelo de threads:
 *   • Thread da tela  (1)             — SDL2: poll de eventos + draw
 *   • Threads de cálculo (COMPUTE_THREADS = 7) — spin loop contínuo na GPU
 *
 *   As 7 threads de cálculo nunca dormem: fazem spin nas barreiras entre fases.
 *   O top/htop mostrará 100% em cada uma delas.
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "config.h"
#include "gpu.h"
#include "geom_shader.h"
#include "frag_shader.h"
#include "janela.h"
#include "shared_frame.h"

int main() {
    const size_t N = (size_t)(FB_W * FB_H);

    // ── Inicializa GPU ────────────────────────────────────────────────────
    GPUManager gpu(NUM_CORES, N);
    init_geometry_vram(gpu.vram(), N);

    const auto geom_prog = build_geometry_shader((uint32_t)N, FB_W, FB_H);
    const auto frag_prog = build_fragment_shader((uint32_t)NUM_CORES, (uint32_t)N);

    std::cout << "GPU-V | geom: " << geom_prog.size() << " instr"
              << " | frag: "      << frag_prog.size() << " instr"
              << " | " << FB_W << "x" << FB_H
              << " | " << NUM_CORES << " nucleos"
              << " | 1 thread tela + " << COMPUTE_THREADS << " threads calculo (spin 100%)\n";

    gpu.set_programs(geom_prog, frag_prog);

    // ── Buffer compartilhado tela ↔ cálculo ──────────────────────────────
    SharedFrame shared;
    shared.pixels.resize(N);

    // ── Thread da tela (SDL2) — consome SharedFrame ───────────────────────
    std::thread screen_thread([&]() {
        Janela janela("GPU-V | Cubo 3D  —  ESC para sair", FB_W, FB_H, SCALE);

        while (true) {
            if (!janela.poll_events()) {
                std::lock_guard<std::mutex> lk(shared.mtx);
                shared.stop = true;
                shared.cv.notify_all();
                break;
            }
            // Aguarda novo frame por até 16 ms (mantém poll de eventos responsivo)
            std::unique_lock<std::mutex> lk(shared.mtx);
            if (shared.cv.wait_for(lk, std::chrono::milliseconds(16),
                                   [&]{ return shared.ready || shared.stop; })) {
                if (shared.stop) break;
                janela.draw(shared.pixels);
                shared.ready = false;
            }
        }
    });

    // ── 7 threads de cálculo em spin loop — bloqueia até shared.stop ─────
    gpu.run_continuous(shared);

    screen_thread.join();
    return 0;
}
