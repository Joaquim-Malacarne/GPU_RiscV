#include "gpu.h"
#include <future>
#include <thread>
#include <algorithm>

// ── Thread de núcleo ──────────────────────────────────────────────────────────
// Cada instância desta função roda em sua própria thread e processa um núcleo.
void GPUManager::run_core(RV32ICore* core) {
    core->execute();
}

// ── Thread de controle ────────────────────────────────────────────────────────
// dispatch_frame() é chamado pela thread principal (controle).
// Ela divide os núcleos em batches, lança as threads de núcleo e aguarda cada batch.
void GPUManager::dispatch_frame() {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;

    for (size_t batch_start = 0; batch_start < num_cores; batch_start += hw) {
        size_t batch_end = std::min(batch_start + (size_t)hw, num_cores);

        // Lança uma thread de núcleo por core no batch
        std::vector<std::future<void>> batch;
        batch.reserve(batch_end - batch_start);
        for (size_t i = batch_start; i < batch_end; ++i)
            batch.push_back(
                std::async(std::launch::async, run_core, &processing_units[i]));

        // Thread de controle bloqueia até todos os workers do batch terminarem
        for (auto& f : batch) f.get();
    }

    swap_buffers();
}

// ─────────────────────────────────────────────────────────────────────────────

GPUManager::GPUManager(size_t cores_count, size_t pixels)
    : num_cores(cores_count), total_pixels(pixels),
      back_buffer(2 * pixels), front_buffer(pixels) {
    processing_units.reserve(cores_count);
    for (size_t i = 0; i < cores_count; ++i)
        processing_units.emplace_back((uint32_t)i, back_buffer);
}

void GPUManager::load_program(const std::vector<uint32_t>& prog) {
    for (auto& core : processing_units)
        core.load_program(prog);
}

void GPUManager::set_mask(const std::vector<uint32_t>& mask) {
    size_t n = std::min(mask.size(), total_pixels);
    for (size_t i = 0; i < n; ++i)
        back_buffer.memory[total_pixels + i] = mask[i];
}

void GPUManager::swap_buffers() {
    // Copia somente a região de cor; a máscara permanece intacta
    std::copy(back_buffer.memory.begin(),
              back_buffer.memory.begin() + (ptrdiff_t)total_pixels,
              front_buffer.memory.begin());
    std::fill(back_buffer.memory.begin(),
              back_buffer.memory.begin() + (ptrdiff_t)total_pixels, 0u);
}

const std::vector<uint32_t>& GPUManager::get_pixels() const {
    return front_buffer.memory;
}
