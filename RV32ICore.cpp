#include "RV32ICore.h"
#include <algorithm>
#include <iostream>
#include <cstdint>

RV32ICore::RV32ICore(uint32_t id, VRAM& vram)
    : pc(0), mhartid(id), vram_ref(vram) {
    std::fill(std::begin(registers), std::end(registers), 0u);
    registers[10] = mhartid; // a0 = mhartid (ABI RISC-V)
}

void RV32ICore::load_program(const std::vector<uint32_t>& program) {
    instruction_memory = program;
    pc = 0;
    std::fill(std::begin(registers), std::end(registers), 0u);
    registers[10] = mhartid;
}

bool RV32ICore::step() {
    uint32_t word_idx = pc / 4;
    if (word_idx >= instruction_memory.size()) return false;

    // ── FETCH ──────────────────────────────────────────────────────────────
    uint32_t inst = instruction_memory[word_idx];

    // ── DECODE ─────────────────────────────────────────────────────────────
    uint32_t opcode = inst & 0x7F;
    uint32_t rd     = (inst >> 7)  & 0x1F;
    uint32_t funct3 = (inst >> 12) & 0x07;
    uint32_t rs1    = (inst >> 15) & 0x1F;
    uint32_t rs2    = (inst >> 20) & 0x1F;
    uint32_t funct7 = (inst >> 25) & 0x7F;

    uint32_t next_pc = pc + 4;

    // ── EXECUTE ────────────────────────────────────────────────────────────
    switch (opcode) {

        // ── Tipo-R (ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND) ────
        case 0x33: {
            uint32_t a = registers[rs1];
            uint32_t b = registers[rs2];
            switch ((funct7 << 3) | funct3) {
                case 0x000: registers[rd] = a + b;                     break; // ADD
                case 0x200: registers[rd] = a - b;                     break; // SUB
                case 0x001: registers[rd] = a << (b & 0x1F);           break; // SLL
                case 0x002: registers[rd] = ((int32_t)a < (int32_t)b); break; // SLT
                case 0x003: registers[rd] = (a < b);                   break; // SLTU
                case 0x004: registers[rd] = a ^ b;                     break; // XOR
                case 0x005: registers[rd] = a >> (b & 0x1F);           break; // SRL
                case 0x205: registers[rd] = (int32_t)a >> (b & 0x1F); break; // SRA
                case 0x006: registers[rd] = a | b;                     break; // OR
                case 0x007: registers[rd] = a & b;                     break; // AND
            }
            break;
        }

        // ── Tipo-I: ALU imediata (ADDI, SLTI, XORI, ORI, ANDI, SLLI, SRLI, SRAI) ─
        case 0x13: {
            uint32_t a    = registers[rs1];
            int32_t  imm  = imm_I(inst);
            uint32_t shamt = rs2; // bits [24:20]
            switch (funct3) {
                case 0x0: registers[rd] = a + (uint32_t)imm;            break; // ADDI
                case 0x1: registers[rd] = a << shamt;                   break; // SLLI
                case 0x2: registers[rd] = ((int32_t)a < imm);           break; // SLTI
                case 0x3: registers[rd] = (a < (uint32_t)imm);          break; // SLTIU
                case 0x4: registers[rd] = a ^ (uint32_t)imm;            break; // XORI
                case 0x5:
                    if (funct7 == 0x20)
                        registers[rd] = (int32_t)a >> shamt;            // SRAI
                    else
                        registers[rd] = a >> shamt;                     // SRLI
                    break;
                case 0x6: registers[rd] = a | (uint32_t)imm;            break; // ORI
                case 0x7: registers[rd] = a & (uint32_t)imm;            break; // ANDI
            }
            break;
        }

        // ── LUI ────────────────────────────────────────────────────────────
        case 0x37:
            registers[rd] = (uint32_t)imm_U(inst);
            break;

        // ── AUIPC ──────────────────────────────────────────────────────────
        case 0x17:
            registers[rd] = pc + (uint32_t)imm_U(inst);
            break;

        // ── JAL ────────────────────────────────────────────────────────────
        case 0x6F:
            registers[rd] = next_pc;
            next_pc = pc + (uint32_t)imm_J(inst);
            break;

        // ── JALR ───────────────────────────────────────────────────────────
        case 0x67:
            if (funct3 == 0x0) {
                uint32_t ret = next_pc;
                next_pc = (registers[rs1] + (uint32_t)imm_I(inst)) & ~1u;
                registers[rd] = ret;
            }
            break;

        // ── Branches (BEQ, BNE, BLT, BGE, BLTU, BGEU) ─────────────────────
        case 0x63: {
            int32_t  a  = (int32_t)registers[rs1];
            int32_t  b  = (int32_t)registers[rs2];
            uint32_t ua = registers[rs1];
            uint32_t ub = registers[rs2];
            bool taken = false;
            switch (funct3) {
                case 0x0: taken = (a  == b);  break; // BEQ
                case 0x1: taken = (a  != b);  break; // BNE
                case 0x4: taken = (a  <  b);  break; // BLT
                case 0x5: taken = (a  >= b);  break; // BGE
                case 0x6: taken = (ua <  ub); break; // BLTU
                case 0x7: taken = (ua >= ub); break; // BGEU
            }
            if (taken)
                next_pc = pc + (uint32_t)imm_B(inst);
            break;
        }

        // ── Loads — lê da VRAM via MMIO (LB, LH, LW, LBU, LHU) ───────────
        case 0x03: {
            uint32_t addr = registers[rs1] + (uint32_t)imm_I(inst);
            uint32_t data = vram_ref.read_pixel(addr);
            switch (funct3) {
                case 0x0: registers[rd] = (uint32_t)(int32_t)(int8_t) (data & 0xFF);   break; // LB
                case 0x1: registers[rd] = (uint32_t)(int32_t)(int16_t)(data & 0xFFFF); break; // LH
                case 0x2: registers[rd] = data;                                         break; // LW
                case 0x4: registers[rd] = data & 0xFF;                                  break; // LBU
                case 0x5: registers[rd] = data & 0xFFFF;                                break; // LHU
            }
            break;
        }

        // ── Stores — escreve na VRAM via MMIO (SW) ─────────────────────────
        case 0x23: {
            uint32_t addr = registers[rs1] + (uint32_t)imm_S(inst);
            if (funct3 == 0x2) // SW — envia 32 bits RGBA ao framebuffer
                vram_ref.write_pixel(addr, registers[rs2]);
            break;
        }

        // ── ECALL / EBREAK — encerra o núcleo ──────────────────────────────
        case 0x73:
            return false;

        // ── Instrução desconhecida ──────────────────────────────────────────
        default:
            std::cerr << "[Core " << mhartid << "] Opcode nao implementado: 0x"
                      << std::hex << opcode << std::dec << "\n";
            return false;
    }

    // x0 é hardwired zero no RISC-V
    registers[0] = 0;

    pc = next_pc;
    return true;
}

void RV32ICore::execute() {
    while (step()) {}
}