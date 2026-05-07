#ifndef RV32ICORE_H
#define RV32ICORE_H

#include <cstdint>
#include <vector>
#include "VRAM.h"

class RV32ICore {
private:
    uint32_t pc;
    uint32_t registers[32];
    uint32_t mhartid;
    VRAM& vram_ref;

    std::vector<uint32_t> instruction_memory;

    int32_t imm_I(uint32_t inst) { return (int32_t)inst >> 20; }
    int32_t imm_S(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1F) | (((inst >> 25) & 0x7F) << 5);
        return (imm & 0x800) ? (int32_t)(imm | 0xFFFFF000) : (int32_t)imm;
    }
    int32_t imm_B(uint32_t inst) {
        uint32_t imm = ((inst >> 8)  & 0xF)  << 1  |
                       ((inst >> 25) & 0x3F)  << 5  |
                       ((inst >> 7)  & 0x1)   << 11 |
                       ((inst >> 31) & 0x1)   << 12;
        return (imm & 0x1000) ? (int32_t)(imm | 0xFFFFE000) : (int32_t)imm;
    }
    int32_t imm_U(uint32_t inst) { return (int32_t)(inst & 0xFFFFF000); }
    int32_t imm_J(uint32_t inst) {
        uint32_t imm = ((inst >> 21) & 0x3FF) << 1  |
                       ((inst >> 20) & 0x1)   << 11 |
                       ((inst >> 12) & 0xFF)  << 12 |
                       ((inst >> 31) & 0x1)   << 20;
        return (imm & 0x100000) ? (int32_t)(imm | 0xFFE00000) : (int32_t)imm;
    }

public:
    RV32ICore(uint32_t id, VRAM& vram);
    void load_program(const std::vector<uint32_t>& program);
    bool step();
    void execute();

    uint32_t get_register(int i) const { return registers[i]; }
    uint32_t get_pc() const { return pc; }
    uint32_t get_id() const { return mhartid; }
};

#endif // RV32ICORE_H