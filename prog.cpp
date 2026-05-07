#include "prog.h"
#include "RV32Asm.h"

/*
 * Loop do shader (9 instruções — offsets fixos dentro do corpo):
 *
 *  +0:  ADD  t4, t1, t3        output_addr = base_addr + i
 *  +4:  ADD  t5, t2, t4        mask_addr   = mask_base + output_addr
 *  +8:  LW   t6, 0(t5)         mask_val    = VRAM[mask_addr]
 *  +12: BEQ  t6, x0, +12       se mask==0 → salta para SW preto em +24
 *  +16: SW   s0, 0(t4)         VRAM[output_addr] = azul
 *  +20: BEQ  x0, x0, +8        salta para ADDI em +28
 *  +24: SW   x0, 0(t4)         VRAM[output_addr] = preto (0)
 *  +28: ADDI t3, t3, 1         i++
 *  +32: BNE  t3, t0, -32       se i != ppc → volta para +0
 *  +36: ECALL
 */
std::vector<uint32_t> build_cube_shader(uint32_t num_cores, uint32_t total_pixels) {
    using namespace RV32Asm;
    std::vector<uint32_t> prog;

    const uint32_t ppc = total_pixels / num_cores;

    // ── Carrega ppc em t0 (x5) via LUI + ADDI ────────────────────────────────
    uint32_t ppc_hi = (ppc >> 12) & 0xFFFFF;
    int32_t  ppc_lo = (int32_t)(ppc & 0xFFF);
    if (ppc_lo >= 2048) { ppc_hi++; ppc_lo -= 4096; }
    prog.push_back(LUI (5, (int)ppc_hi));
    prog.push_back(ADDI(5, 5, ppc_lo));

    // ── base_addr (t1) = mhartid << log2(ppc)  [ppc deve ser potência de 2] ─
    uint32_t shift = 0;
    for (uint32_t tmp = ppc; tmp > 1; tmp >>= 1) shift++;
    prog.push_back(ADDI(30, 0, (int32_t)shift)); // t5 = shift (temporário)
    prog.push_back(SLL (6, 10, 30));             // t1 = mhartid << shift

    // ── mask_base (t2) = total_pixels via LUI + ADDI ──────────────────────────
    uint32_t tb_hi = (total_pixels >> 12) & 0xFFFFF;
    int32_t  tb_lo = (int32_t)(total_pixels & 0xFFF);
    if (tb_lo >= 2048) { tb_hi++; tb_lo -= 4096; }
    prog.push_back(LUI (7, (int)tb_hi));
    prog.push_back(ADDI(7, 7, tb_lo));

    // ── s0 (x8) = azul = 0x0000FFFF  (R=0 G=0 B=255 A=255) ──────────────────
    prog.push_back(LUI (8, 0x10));   // s0 = 0x00010000
    prog.push_back(ADDI(8, 8, -1));  // s0 = 0x0000FFFF

    // ── i (t3) = 0 ────────────────────────────────────────────────────────────
    prog.push_back(ADDI(28, 0, 0));

    // ── corpo do loop ─────────────────────────────────────────────────────────
    prog.push_back(ADD (29, 6,  28));   // t4 = base_addr + i
    prog.push_back(ADD (30, 7,  29));   // t5 = mask_base + output_addr
    prog.push_back(LW  (31, 30,  0));   // t6 = VRAM[mask_addr]
    prog.push_back(BEQ (31,  0, +12)); // se mask==0 → SW preto (+12 bytes)
    prog.push_back(SW  ( 8, 29,  0));   // escreve azul
    prog.push_back(BEQ ( 0,  0,  +8)); // salta sobre SW preto (+8 bytes)
    prog.push_back(SW  ( 0, 29,  0));  // escreve preto
    prog.push_back(ADDI(28, 28,  1));   // i++
    prog.push_back(BNE (28,  5, -32)); // se i != ppc → volta ao início do loop
    prog.push_back(ECALL());

    return prog;
}
