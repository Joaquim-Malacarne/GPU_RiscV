#ifndef RV32ASM_H
#define RV32ASM_H

#include <cstdint>

// Mini-montador RV32I + extensão M
// Cada função retorna a palavra de instrução de 32 bits.
namespace RV32Asm {

    // ── Tipo-R base ──────────────────────────────────────────────────────────
    inline uint32_t ADD (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x0<<12)|(rd<<7)|0x33; }
    inline uint32_t SUB (int rd,int rs1,int rs2){ return (0x20<<25)|(rs2<<20)|(rs1<<15)|(0x0<<12)|(rd<<7)|0x33; }
    inline uint32_t SLL (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x1<<12)|(rd<<7)|0x33; }
    inline uint32_t SLT (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x2<<12)|(rd<<7)|0x33; }
    inline uint32_t SLTU(int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x3<<12)|(rd<<7)|0x33; }
    inline uint32_t XOR (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x4<<12)|(rd<<7)|0x33; }
    inline uint32_t SRL (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x33; }
    inline uint32_t SRA (int rd,int rs1,int rs2){ return (0x20<<25)|(rs2<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x33; }
    inline uint32_t OR  (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x6<<12)|(rd<<7)|0x33; }
    inline uint32_t AND (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x7<<12)|(rd<<7)|0x33; }

    // ── Extensão M (funct7 = 0x01) ───────────────────────────────────────────
    inline uint32_t MUL  (int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x0<<12)|(rd<<7)|0x33; }
    inline uint32_t MULH (int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x1<<12)|(rd<<7)|0x33; }
    inline uint32_t MULHSU(int rd,int rs1,int rs2){return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x2<<12)|(rd<<7)|0x33; }
    inline uint32_t MULHU(int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x3<<12)|(rd<<7)|0x33; }
    inline uint32_t DIV  (int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x4<<12)|(rd<<7)|0x33; }
    inline uint32_t DIVU (int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x33; }
    inline uint32_t REM  (int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x6<<12)|(rd<<7)|0x33; }
    inline uint32_t REMU (int rd,int rs1,int rs2){ return (0x01<<25)|(rs2<<20)|(rs1<<15)|(0x7<<12)|(rd<<7)|0x33; }

    // ── Tipo-I: ALU imediata ─────────────────────────────────────────────────
    inline uint32_t ADDI (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x0<<12)|(rd<<7)|0x13; }
    inline uint32_t SLTI (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x2<<12)|(rd<<7)|0x13; }
    inline uint32_t SLTIU(int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x3<<12)|(rd<<7)|0x13; }
    inline uint32_t XORI (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x4<<12)|(rd<<7)|0x13; }
    inline uint32_t ORI  (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x6<<12)|(rd<<7)|0x13; }
    inline uint32_t ANDI (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x7<<12)|(rd<<7)|0x13; }
    // Shifts imediatos: shamt = bits [4:0] do campo imm
    inline uint32_t SLLI (int rd,int rs1,int sh) { return ((sh&0x1F)<<20)|(rs1<<15)|(0x1<<12)|(rd<<7)|0x13; }
    inline uint32_t SRLI (int rd,int rs1,int sh) { return ((sh&0x1F)<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x13; }
    inline uint32_t SRAI (int rd,int rs1,int sh) { return (0x20<<25)|((sh&0x1F)<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x13; }

    // ── Tipo-I: loads ────────────────────────────────────────────────────────
    inline uint32_t LB  (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x0<<12)|(rd<<7)|0x03; }
    inline uint32_t LH  (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x1<<12)|(rd<<7)|0x03; }
    inline uint32_t LW  (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x2<<12)|(rd<<7)|0x03; }
    inline uint32_t LBU (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x4<<12)|(rd<<7)|0x03; }
    inline uint32_t LHU (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x03; }

    // ── Tipo-U ───────────────────────────────────────────────────────────────
    inline uint32_t LUI  (int rd,int imm20){ return ((imm20&0xFFFFF)<<12)|(rd<<7)|0x37; }
    inline uint32_t AUIPC(int rd,int imm20){ return ((imm20&0xFFFFF)<<12)|(rd<<7)|0x17; }

    // ── Tipo-S: stores ───────────────────────────────────────────────────────
    inline uint32_t SW(int rs2,int rs1,int imm){
        return (((imm>>5)&0x7F)<<25)|(rs2<<20)|(rs1<<15)|(0x2<<12)|((imm&0x1F)<<7)|0x23; }

    // ── Tipo-B: branches ─────────────────────────────────────────────────────
    // off = deslocamento em bytes (múltiplo de 2, tipicamente múltiplo de 4)
    inline uint32_t _branch(int rs1,int rs2,int f3,int o){
        return (((o>>12)&1)<<31)|(((o>>5)&0x3F)<<25)|(rs2<<20)|(rs1<<15)
              |(f3<<12)|(((o>>1)&0xF)<<8)|(((o>>11)&1)<<7)|0x63; }
    inline uint32_t BEQ (int rs1,int rs2,int off){ return _branch(rs1,rs2,0x0,off); }
    inline uint32_t BNE (int rs1,int rs2,int off){ return _branch(rs1,rs2,0x1,off); }
    inline uint32_t BLT (int rs1,int rs2,int off){ return _branch(rs1,rs2,0x4,off); }
    inline uint32_t BGE (int rs1,int rs2,int off){ return _branch(rs1,rs2,0x5,off); }
    inline uint32_t BLTU(int rs1,int rs2,int off){ return _branch(rs1,rs2,0x6,off); }
    inline uint32_t BGEU(int rs1,int rs2,int off){ return _branch(rs1,rs2,0x7,off); }

    // ── Tipo-J / I: jumps ────────────────────────────────────────────────────
    inline uint32_t JAL(int rd,int off){
        int o=off;
        return (((o>>20)&1)<<31)|(((o>>1)&0x3FF)<<21)|(((o>>11)&1)<<20)
              |(((o>>12)&0xFF)<<12)|(rd<<7)|0x6F; }
    inline uint32_t JALR(int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(rd<<7)|0x67; }

    // ── Sistema ──────────────────────────────────────────────────────────────
    inline uint32_t ECALL(){ return 0x00000073; }

} // namespace RV32Asm

#endif // RV32ASM_H
