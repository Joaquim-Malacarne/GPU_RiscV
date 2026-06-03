#include "frag_shader.h"
#include "RV32Asm.h"

std::vector<uint32_t> build_fragment_shader(uint32_t num_cores, uint32_t N) {
    using namespace RV32Asm;
    std::vector<uint32_t> prog;

    const uint32_t ppc = N / num_cores; // pixels por núcleo

    // ── Carrega ppc em t0 via LUI + ADDI ──────────────────────────────────
    uint32_t ppc_hi = (ppc >> 12) & 0xFFFFF;
    int32_t  ppc_lo = (int32_t)(ppc & 0xFFF);
    if (ppc_lo >= 2048) { ppc_hi++; ppc_lo -= 4096; }
    prog.push_back(LUI (5, (int)ppc_hi));
    prog.push_back(ADDI(5, 5, ppc_lo));

    // ── base_addr (t1) = mhartid × ppc (MUL: funciona para qualquer ppc) ──
    prog.push_back(MUL(6, 10, 5));  // t1 = mhartid * ppc

    // ── mask_base (t2) = N via LUI + ADDI ────────────────────────────────
    uint32_t n_hi = (N >> 12) & 0xFFFFF;
    int32_t  n_lo = (int32_t)(N & 0xFFF);
    if (n_lo >= 2048) { n_hi++; n_lo -= 4096; }
    prog.push_back(LUI (7, (int)n_hi));
    prog.push_back(ADDI(7, 7, n_lo));

    // ── azul = 0x0000FFFF em s0 ───────────────────────────────────────────
    prog.push_back(LUI (8, 0x10));   // s0 = 0x00010000
    prog.push_back(ADDI(8, 8, -1)); // s0 = 0x0000FFFF

    // ── i (t3) = 0 ────────────────────────────────────────────────────────
    prog.push_back(ADDI(28, 0, 0));

    // ── loop (9 instruções, 36 bytes) ─────────────────────────────────────
    //  +0:  t4 = base_addr + i
    //  +4:  t5 = mask_base + t4
    //  +8:  t6 = VRAM[t5]
    //  +12: if t6 == 0 → pula para SW preto (+12)
    //  +16: SW azul
    //  +20: BEQ x0,x0 → pula sobre SW preto (+8)
    //  +24: SW preto
    //  +28: i++
    //  +32: if i != ppc → volta (-32)
    prog.push_back(ADD (29, 6,  28));   // t4 = base_addr + i
    prog.push_back(ADD (30, 7,  29));   // t5 = mask_base + t4
    prog.push_back(LW  (31, 30,  0));   // t6 = VRAM[t5]
    prog.push_back(BEQ (31,  0, +12)); // mask==0 → SW preto
    prog.push_back(SW  ( 8, 29,  0));   // escreve azul
    prog.push_back(BEQ ( 0,  0,  +8)); // pula sobre SW preto
    prog.push_back(SW  ( 0, 29,  0));   // escreve preto
    prog.push_back(ADDI(28, 28,  1));   // i++
    prog.push_back(BNE (28,  5, -32)); // i != ppc → volta ao início do loop
    prog.push_back(ECALL());

    return prog;
}
