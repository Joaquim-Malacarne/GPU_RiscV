/*
 * GPU-V  —  Emulador de GPU baseado em RISC-V RV32I+M
 * Universidade Católica de Santos
 *
 * main.cpp: orquestra inicialização e loop de animação.
 *
 * Toda a matemática 3D (rotação, projeção, Bresenham) roda nos núcleos RV32I.
 * A CPU apenas atualiza o ângulo e dispara os shaders.
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "config.h"
#include "gpu.h"
#include "geom_shader.h"
#include "frag_shader.h"
#include "janela.h"

int main() {
    const size_t N = (size_t)(FB_W * FB_H);   // total de pixels

    // ── Inicializa GPU ────────────────────────────────────────────────────
    GPUManager gpu(NUM_CORES, N, NUM_THREADS);

    // Grava sin/cos LUT e vértices do cubo na VRAM (uma única vez)
    init_geometry_vram(gpu.vram(), N);

    // Compila shaders RV32I uma única vez
    const auto geom_prog = build_geometry_shader((uint32_t)N, FB_W, FB_H);
    const auto frag_prog = build_fragment_shader((uint32_t)NUM_CORES, (uint32_t)N);

    std::cout << "GPU-V | geom shader: " << geom_prog.size() << " instr"
              << " | frag shader: "      << frag_prog.size() << " instr"
              << " | " << FB_W << "x" << FB_H
              << " | " << NUM_CORES << " nucleos"
              << " | threads: " << (NUM_THREADS ? NUM_THREADS : (int)std::thread::hardware_concurrency())
              << "\n";

    // Pré-carrega o fragment shader em todos os núcleos (não muda entre frames)
    gpu.load_program(frag_prog);

    // ── Janela SDL2 ───────────────────────────────────────────────────────
    Janela janela("GPU-V | Cubo 3D  —  ESC para sair", FB_W, FB_H, SCALE);

    uint32_t angle_idx = 0;

    // ── Loop principal ────────────────────────────────────────────────────
    while (janela.poll_events()) {

        // 1. CPU grava o ângulo atual na VRAM
        gpu.set_angle(angle_idx);

        // 2. Geometry shader (núcleo 0): rotação + projeção + Bresenham
        gpu.load_geometry(geom_prog, 0);
        gpu.dispatch_geometry(0);

        // 3. Fragment shader (64 núcleos): lê máscara, pinta pixels
        gpu.load_program(frag_prog);
        gpu.dispatch_frame();

        // 4. Exibe o frame
        janela.draw(gpu.get_pixels());

        // Avança ~0.6° por frame (256 passos = volta completa ≈ 14s)
        angle_idx = (angle_idx + 1) & 0xFF;

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
