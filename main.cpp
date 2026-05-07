/*
 * GPU-V  —  Emulador de GPU baseado em RISC-V RV32I
 * Universidade Católica de Santos
 *
 * main.cpp: loop principal de animação.
 *   gpu.cpp    → gerenciamento de núcleos e threads
 *   prog.cpp   → programa shader RV32I
 *   math3d.cpp → transformações 3D e rasterização de arestas
 *   janela.cpp → componente gráfico SDL2
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

#include "gpu.h"
#include "prog.h"
#include "math3d.h"
#include "janela.h"

static const int    FB_W         = 128;
static const int    FB_H         = 128;
static const int    SCALE        = 5;
static const size_t TOTAL_PIXELS = (size_t)(FB_W * FB_H);
static const size_t NUM_CORES    = 64;   // ppc = 256 = 2^8

int main() {
    GPUManager gpu(NUM_CORES, TOTAL_PIXELS);
    const auto shader = build_cube_shader((uint32_t)NUM_CORES, (uint32_t)TOTAL_PIXELS);

    std::cout << "GPU-V | shader: " << shader.size() << " instruções"
              << " | " << FB_W << "×" << FB_H
              << " | " << NUM_CORES << " núcleos"
              << " | " << TOTAL_PIXELS / NUM_CORES << " px/núcleo\n";

    Janela janela("GPU-V | Cubo 3D  —  ESC para sair", FB_W, FB_H, SCALE);

    std::vector<uint32_t> mask(TOTAL_PIXELS);
    float verts[8][3];
    float sv[8][2];
    float angle = 0.0f;

    while (janela.poll_events()) {
        // ── CPU: transforma vértices ──────────────────────────────────────────
        for (int v = 0; v < 8; v++) {
            verts[v][0] = CUBE_VERTS[v][0];
            verts[v][1] = CUBE_VERTS[v][1];
            verts[v][2] = CUBE_VERTS[v][2];
            rotate_yx(verts[v], angle * 0.6f, angle);
        }

        // ── CPU: projeta e rasteriza arestas na máscara ───────────────────────
        for (int v = 0; v < 8; v++)
            project(verts[v], FB_W, FB_H, sv[v][0], sv[v][1]);

        std::fill(mask.begin(), mask.end(), 0u);
        for (int e = 0; e < 12; e++)
            bresenham(mask, FB_W, FB_H,
                      (int)sv[CUBE_EDGES[e][0]][0], (int)sv[CUBE_EDGES[e][0]][1],
                      (int)sv[CUBE_EDGES[e][1]][0], (int)sv[CUBE_EDGES[e][1]][1]);

        // ── GPU: grava máscara, carrega shader, processa frame ────────────────
        gpu.set_mask(mask);
        gpu.load_program(shader);
        gpu.dispatch_frame();

        // ── Exibe ─────────────────────────────────────────────────────────────
        janela.draw(gpu.get_pixels());

        angle += 0.02f;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
