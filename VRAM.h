#ifndef VRAM_H
#define VRAM_H

#include <vector>
#include <cstdint>
#include <atomic>

// VRAM linear: cada posição = 1 pixel RGBA8888 (32 bits)
// Escrita lock-free é segura pois cada núcleo escreve em regiões disjuntas
// (particionadas pelo mhartid).
class VRAM {
public:
    std::vector<uint32_t> memory;

    explicit VRAM(size_t size) : memory(size, 0) {}

    void write_pixel(uint32_t address, uint32_t rgba) {
        if (address < memory.size())
            memory[address] = rgba;
    }

    uint32_t read_pixel(uint32_t address) const {
        if (address < memory.size()) return memory[address];
        return 0;
    }

    size_t size() const { return memory.size(); }
};

#endif // VRAM_H