#ifndef RV32ASM_H
#define RV32ASM_H

#include <cstdint>

// Mini-montador RV32I: cada função retorna a palavra de instrução de 32 bits
namespace RV32Asm {

    // ── Tipo-R ───────────────────────────────────────────────────────────────
    inline uint32_t ADD (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(rd<<7)|0x33; }
    inline uint32_t SLL (int rd,int rs1,int rs2){ return (rs2<<20)|(rs1<<15)|(0x1<<12)|(rd<<7)|0x33; }

    // ── Tipo-I ───────────────────────────────────────────────────────────────
    inline uint32_t ADDI(int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(rd<<7)|0x13; }
    inline uint32_t LW  (int rd,int rs1,int imm){ return ((imm&0xFFF)<<20)|(rs1<<15)|(0x2<<12)|(rd<<7)|0x03; }

    // ── Tipo-U ───────────────────────────────────────────────────────────────
    inline uint32_t LUI (int rd,int imm20)      { return ((imm20&0xFFFFF)<<12)|(rd<<7)|0x37; }

    // ── Tipo-S ───────────────────────────────────────────────────────────────
    inline uint32_t SW  (int rs2,int rs1,int imm){
        return (((imm>>5)&0x7F)<<25)|(rs2<<20)|(rs1<<15)|(0x2<<12)|((imm&0x1F)<<7)|0x23; }

    // ── Tipo-B ───────────────────────────────────────────────────────────────
    inline uint32_t BEQ (int rs1,int rs2,int off){
        int o=off;
        return (((o>>12)&1)<<31)|(((o>>5)&0x3F)<<25)|(rs2<<20)|(rs1<<15)
              |(((o>>1)&0xF)<<8)|(((o>>11)&1)<<7)|0x63; }
    inline uint32_t BNE (int rs1,int rs2,int off){
        int o=off;
        return (((o>>12)&1)<<31)|(((o>>5)&0x3F)<<25)|(rs2<<20)|(rs1<<15)
              |(0x1<<12)|(((o>>1)&0xF)<<8)|(((o>>11)&1)<<7)|0x63; }

    // ── Sistema ──────────────────────────────────────────────────────────────
    inline uint32_t ECALL(){ return 0x00000073; }

} // namespace RV32Asm

#endif // RV32ASM_H
