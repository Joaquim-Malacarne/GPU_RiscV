#include "geom_shader.h"
#include "vram_layout.h"
#include "RV32Asm.h"
#include "config.h"
#include <cmath>

/*
 * Registradores usados pelo geometry shader:
 *   x4  (tp)  = N (total_pixels)
 *   x5  (t0)  = sin_base  = 2N+1
 *   x6  (t1)  = cos_base  = 2N+257
 *   x7  (t2)  = verts_base = 2N+513
 *   x8  (s0)  = screen_base = 2N+537
 *   x9  (s1)  = W (128)
 *   x10 (a0)  = temporário / angle_idx / d
 *   x11 (a1)  = sin_y
 *   x12 (a2)  = cos_y
 *   x13 (a3)  = sin_x
 *   x14 (a4)  = cos_x
 *   x15-x17   = vx, vy, vz (vertex loop)
 *   x18-x23   = vx', vy', vz', sx, sy (vertex/bresenham)
 *   x24 (s8)  = vertex loop counter
 *   x26 (s10) = Bresenham x0
 *   x27 (s11) = Bresenham y0
 *   x17       = Bresenham x1 (reusa)
 *   x18       = Bresenham y1 (reusa)
 *   x19 (s3)  = Bresenham dx
 *   x20 (s4)  = Bresenham dy
 *   x21 (s5)  = Bresenham step_x
 *   x22 (s6)  = Bresenham step_y
 *   x23 (s7)  = Bresenham err
 *   x28-x31   = temporários
 */

// Vértices do cubo unitário [-1,1]^3 em ponto fixo Q8.8 (escala 256)
static const int CUBE_VERTS_FP[8][3] = {
    {-256,-256,-256}, { 256,-256,-256}, { 256, 256,-256}, {-256, 256,-256},
    {-256,-256, 256}, { 256,-256, 256}, { 256, 256, 256}, {-256, 256, 256}
};

static const int CUBE_EDGES[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7}
};

// ── Inicialização da VRAM ─────────────────────────────────────────────────────

void init_geometry_vram(VRAM& vram, size_t N) {
    // sin/cos LUT: 256 entradas, escala Q8.8 (×256), índice i → ângulo 2π×i/256
    size_t sin_base = vl_sin_base(N);
    size_t cos_base = vl_cos_base(N);
    for (int i = 0; i < 256; i++) {
        double ang = 2.0 * M_PI * i / 256.0;
        vram.memory[sin_base + i] = (uint32_t)(int32_t)std::round(std::sin(ang) * 256.0);
        vram.memory[cos_base + i] = (uint32_t)(int32_t)std::round(std::cos(ang) * 256.0);
    }

    // Vértices do cubo em Q8.8
    size_t vb = vl_verts_base(N);
    for (int v = 0; v < 8; v++) {
        vram.memory[vb + v*3 + 0] = (uint32_t)(int32_t)CUBE_VERTS_FP[v][0];
        vram.memory[vb + v*3 + 1] = (uint32_t)(int32_t)CUBE_VERTS_FP[v][1];
        vram.memory[vb + v*3 + 2] = (uint32_t)(int32_t)CUBE_VERTS_FP[v][2];
    }
}

// ── Montador do shader ────────────────────────────────────────────────────────

// Emite o bloco Bresenham para uma aresta (v0, v1).
// screen_base está em x8, N está em x4, W está em x9.
// Tamanho fixo: 39 instruções (19 setup + 20 loop).
static void emit_bresenham(std::vector<uint32_t>& p, int v0, int v1,
                           uint32_t W, uint32_t H) {
    using namespace RV32Asm;
    // ── Setup (19 instruções) ──────────────────────────────────────────────
    // x26=x0, x27=y0, x17=x1, x18=y1 (endereços word no screen_verts)
    p.push_back(LW(26, 8, v0*2));       // 1  x26 = sx[v0]
    p.push_back(LW(27, 8, v0*2+1));     // 2  x27 = sy[v0]
    p.push_back(LW(17, 8, v1*2));       // 3  x17 = sx[v1]
    p.push_back(LW(18, 8, v1*2+1));     // 4  x18 = sy[v1]

    // dx = |x1 - x0|  (abs via sign-mask trick)
    p.push_back(SUB (19, 17, 26));      // 5  x19 = x1-x0
    p.push_back(SRAI(30, 19, 31));      // 6  x30 = sign mask
    p.push_back(XOR (19, 19, 30));      // 7
    p.push_back(SUB (19, 19, 30));      // 8  x19 = |dx|

    // dy = |y1 - y0|
    p.push_back(SUB (20, 18, 27));      // 9  x20 = y1-y0
    p.push_back(SRAI(30, 20, 31));      // 10
    p.push_back(XOR (20, 20, 30));      // 11
    p.push_back(SUB (20, 20, 30));      // 12 x20 = |dy|

    // step_x = (x0 < x1) ? 1 : -1  via: SLT→0|1, *2, -1
    p.push_back(SLT (21, 26, 17));      // 13 x21 = 1 se x0<x1
    p.push_back(SLLI(21, 21, 1));       // 14 x21 = 0 ou 2
    p.push_back(ADDI(21, 21, -1));      // 15 x21 = ±1

    // step_y
    p.push_back(SLT (22, 27, 18));      // 16
    p.push_back(SLLI(22, 22, 1));       // 17
    p.push_back(ADDI(22, 22, -1));      // 18 x22 = ±1

    // err = dx - dy
    p.push_back(SUB(23, 19, 20));       // 19 x23 = err

    // ── Loop Bresenham (20 instruções) ──────────────────────────────────────
    // bres_top está aqui (offset +0 relativo ao loop)
    //
    //  +0   SLTIU x30, x26, 128   → x_in_bounds?
    //  +4   BEQ   x30, x0, +32   → se fora, pula para skip_write (+36)
    //  +8   SLTIU x30, x27, 128   → y_in_bounds?
    //  +12  BEQ   x30, x0, +24   → se fora, pula para skip_write (+36)
    //  +16  MUL   x30, x27, x9   → y*W  (x9 = W, funciona para qualquer W)
    //  +20  ADD   x30, x30, x26
    //  +24  ADD   x30, x30, x4    → addr = N + y*W + x
    //  +28  ADDI  x31, x0, 1
    //  +32  SW    x31, x30, 0     → mask[addr] = 1
    // skip_write:
    //  +36  BNE   x26, x17, +8   → se x0!=x1 → not_done (+44)
    //  +40  BEQ   x27, x18, +40  → se y0==y1 → done (+80)
    // not_done:
    //  +44  SLLI  x30, x23, 1    → e2 = 2*err
    //  +48  ADD   x31, x30, x20  → e2+dy
    //  +52  BGE   x0, x31, +12   → se e2+dy<=0 → skip_x (+64)
    //  +56  SUB   x23, x23, x20  → err -= dy
    //  +60  ADD   x26, x26, x21  → x0 += step_x
    // skip_x:
    //  +64  BGE   x30, x19, +12  → se e2>=dx → skip_y (+76)
    //  +68  ADD   x23, x23, x19  → err += dx
    //  +72  ADD   x27, x27, x22  → y0 += step_y
    // skip_y:
    //  +76  BEQ   x0, x0, -76   → volta para bres_top
    // done:
    //  +80  (próximo bloco ou ECALL)

    p.push_back(SLTIU(30, 26, (int)W));  // 20
    p.push_back(BEQ  (30,  0, +32));    // 21 → skip_write
    p.push_back(SLTIU(30, 27, (int)H)); // 22
    p.push_back(BEQ  (30,  0, +24));    // 23 → skip_write
    p.push_back(MUL  (30, 27,  9));     // 24 y*W  (x9 = W, qualquer resolução)
    p.push_back(ADD  (30, 30,  26));    // 25
    p.push_back(ADD  (30, 30,   4));    // 26 +N  (x4=N)
    p.push_back(ADDI (31,  0,   1));    // 27
    p.push_back(SW   (31, 30,   0));    // 28 mask=1
    p.push_back(BNE  (26, 17,  +8));    // 29 → not_done
    p.push_back(BEQ  (27, 18, +40));    // 30 → done
    p.push_back(SLLI (30, 23,   1));    // 31 e2
    p.push_back(ADD  (31, 30,  20));    // 32 e2+dy
    p.push_back(BGE  ( 0, 31, +12));    // 33 → skip_x
    p.push_back(SUB  (23, 23,  20));    // 34 err-=dy
    p.push_back(ADD  (26, 26,  21));    // 35 x0+=step_x
    p.push_back(BGE  (30, 19, +12));    // 36 → skip_y
    p.push_back(ADD  (23, 23,  19));    // 37 err+=dx
    p.push_back(ADD  (27, 27,  22));    // 38 y0+=step_y
    p.push_back(BEQ  ( 0,  0, -76));   // 39 → bres_top
    // done: próximo push_back começa aqui
}

// ── build_geometry_shader ─────────────────────────────────────────────────────

std::vector<uint32_t> build_geometry_shader(uint32_t N, uint32_t W, uint32_t H) {
    using namespace RV32Asm;
    std::vector<uint32_t> p;

    const int half_W  = (int)(W / 2);
    const int half_H  = (int)(H / 2);
    const int proj_W  = (int)std::round(CUBE_SCALE * W);
    const int proj_H  = (int)std::round(CUBE_SCALE * H);

    // ── Prólogo: carregar constantes de base ──────────────────────────────
    // x4 = N
    {
        uint32_t hi = (N >> 12) & 0xFFFFF;
        int32_t  lo = (int32_t)(N & 0xFFF);
        if (lo >= 2048) { hi++; lo -= 4096; }
        p.push_back(LUI (4, (int)hi));
        p.push_back(ADDI(4, 4, lo));
    }
    // x5 = 2N+1, x6 = 2N+257, x7 = 2N+513, x8 = 2N+537
    p.push_back(ADD (5, 4, 4));         // x5 = 2N
    p.push_back(ADDI(5, 5, 1));         // x5 = 2N+1  (sin_base)
    p.push_back(ADDI(6, 5, 256));       // x6 = 2N+257 (cos_base)
    p.push_back(ADDI(7, 6, 256));       // x7 = 2N+513 (verts_base)
    p.push_back(ADDI(8, 7, 24));        // x8 = 2N+537 (screen_base)

    // x9 = W (largura do framebuffer — usado em Bresenham para y*W)
    {
        uint32_t hi = (W >> 12) & 0xFFFFF;
        int32_t  lo = (int32_t)(W & 0xFFF);
        if (lo >= 2048) { hi++; lo -= 4096; }
        if (hi > 0) {
            p.push_back(LUI (9, (int)hi));
            p.push_back(ADDI(9, 9, lo));
        } else {
            p.push_back(ADDI(9, 0, lo));
        }
    }

    // ── Lê angle_idx e carrega sin/cos ───────────────────────────────────
    p.push_back(LW  (10, 5, -1));       // a0 = VRAM[2N] = angle_idx

    // ax_idx = (angle_idx * ROT_X_RATIO) >> 8
    p.push_back(ADDI(28,  0, ROT_X_RATIO));
    p.push_back(MUL (28, 10, 28));
    p.push_back(SRAI(28, 28,  8));      // x28 = ax_idx

    p.push_back(ADD (29,  5, 10));
    p.push_back(LW  (11, 29,  0));      // x11 = sin_y
    p.push_back(ADD (29,  6, 10));
    p.push_back(LW  (12, 29,  0));      // x12 = cos_y
    p.push_back(ADD (29,  5, 28));
    p.push_back(LW  (13, 29,  0));      // x13 = sin_x
    p.push_back(ADD (29,  6, 28));
    p.push_back(LW  (14, 29,  0));      // x14 = cos_x

    // ── Loop sobre 8 vértices ─────────────────────────────────────────────
    // x24 = v = 0
    p.push_back(ADDI(24, 0, 0));

    // vertex_loop_top — 46 instruções (incluindo o BNE final)
    // O BNE precisa de offset = -(45*4) = -180

    p.push_back(ADDI(10,  0,  3));      //  1 const 3
    p.push_back(MUL (10, 24, 10));      //  2 v*3
    p.push_back(ADD (10, 10,  7));      //  3 verts_base + v*3

    p.push_back(LW  (15, 10,  0));      //  4 vx
    p.push_back(LW  (16, 10,  1));      //  5 vy
    p.push_back(LW  (17, 10,  2));      //  6 vz

    // Rotação Y: vx' = (vx*cos_y + vz*sin_y) >> 8
    p.push_back(MUL (28, 15, 12));      //  7 vx*cos_y
    p.push_back(MUL (29, 17, 11));      //  8 vz*sin_y
    p.push_back(ADD (28, 28, 29));      //  9
    p.push_back(SRAI(28, 28,  8));      // 10 x28 = vx'

    // vz_tmp = (-vx*sin_y + vz*cos_y) >> 8
    p.push_back(MUL (29, 15, 11));      // 11 vx*sin_y
    p.push_back(MUL (30, 17, 12));      // 12 vz*cos_y
    p.push_back(SUB (30, 30, 29));      // 13 vz*cos_y - vx*sin_y
    p.push_back(SRAI(30, 30,  8));      // 14 x30 = vz_tmp

    // Rotação X: vy' = (vy*cos_x - vz_tmp*sin_x) >> 8
    p.push_back(MUL (29, 16, 14));      // 15 vy*cos_x
    p.push_back(MUL (31, 30, 13));      // 16 vz_tmp*sin_x
    p.push_back(SUB (29, 29, 31));      // 17
    p.push_back(SRAI(29, 29,  8));      // 18 x29 = vy'

    // vz' = (vy*sin_x + vz_tmp*cos_x) >> 8
    p.push_back(MUL (31, 16, 13));      // 19 vy*sin_x
    p.push_back(MUL (10, 30, 14));      // 20 vz_tmp*cos_x
    p.push_back(ADD (31, 31, 10));      // 21
    p.push_back(SRAI(31, 31,  8));      // 22 x31 = vz'

    // Projeção perspectiva: d = 229376 / (896 + vz')
    // 229376 = 56 << 12
    p.push_back(LUI (10, 56));          // 23 x10 = 229376
    p.push_back(ADDI(31, 31, 896));     // 24 x31 = denom = 896 + vz'
    p.push_back(DIV (10, 10, 31));      // 25 x10 = d

    // sx = ((vx' * d) >> 8) * proj_W >> 8 + W/2
    p.push_back(MUL (30, 28, 10));      // 26 vx'*d
    p.push_back(SRAI(30, 30,  8));      // 27
    p.push_back(ADDI(31,  0, proj_W)); // 28
    p.push_back(MUL (30, 30, 31));      // 29
    p.push_back(SRAI(30, 30,  8));      // 30
    p.push_back(ADDI(21, 30, half_W)); // 31 x21 = sx

    // sy = (-(vy' * d) >> 8) * proj_H >> 8 + H/2
    p.push_back(MUL (30, 29, 10));      // 32 vy'*d
    p.push_back(SRAI(30, 30,  8));      // 33
    p.push_back(SUB (30,  0, 30));      // 34 nega (y invertido para tela)
    p.push_back(ADDI(31,  0, proj_H)); // 35
    p.push_back(MUL (30, 30, 31));      // 36
    p.push_back(SRAI(30, 30,  8));      // 37
    p.push_back(ADDI(22, 30, half_H)); // 38 x22 = sy

    // Escreve (sx, sy) em screen_verts[v*2], screen_verts[v*2+1]
    p.push_back(ADDI(31,  0,  2));      // 39
    p.push_back(MUL (31, 24, 31));      // 40 v*2
    p.push_back(ADD (31, 31,  8));      // 41 screen_base + v*2
    p.push_back(SW  (21, 31,  0));      // 42 sx
    p.push_back(SW  (22, 31,  1));      // 43 sy

    p.push_back(ADDI(24, 24,  1));      // 44 v++
    p.push_back(ADDI(10,  0,  8));      // 45 const 8
    p.push_back(BNE (24, 10, -180));    // 46 se v!=8 → volta (45 insts * 4 = 180)


    // ── 12 blocos Bresenham (39 insts cada) ─────────────────────────────────
    // A edge mask [N,2N) é zerada pela fase 2 do pipeline C++ (paralelo).
    // O geometry shader assume a mask limpa ao entrar.
    for (int e = 0; e < 12; e++)
        emit_bresenham(p, CUBE_EDGES[e][0], CUBE_EDGES[e][1], W, H);

    p.push_back(ECALL());

    return p;
}
