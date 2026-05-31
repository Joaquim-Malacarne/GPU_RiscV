#include "gpu.h"
#include "vram_layout.h"
#include <future>
#include <thread>
#include <algorithm>

void GPUManager::run_core(RV32ICore* core) {
    core->execute();
}

GPUManager::GPUManager(size_t cores_count, size_t pixels, int num_threads_hint)
    : num_cores(cores_count), total_pixels(pixels), num_threads(num_threads_hint),
      back_buffer(vl_total_size(pixels)), front_buffer(pixels) {
    processing_units.reserve(cores_count);
    for (size_t i = 0; i < cores_count; ++i)
        processing_units.emplace_back((uint32_t)i, back_buffer);
}

void GPUManager::load_program(const std::vector<uint32_t>& prog) {
    for (auto& core : processing_units)
        core.load_program(prog);
}

void GPUManager::load_geometry(const std::vector<uint32_t>& prog, size_t core_id) {
    if (core_id < num_cores)
        processing_units[core_id].load_program(prog);
}

void GPUManager::set_angle(uint32_t angle_idx) {
    back_buffer.memory[vl_angle_addr(total_pixels)] = angle_idx;
}

void GPUManager::dispatch_geometry(size_t core_id) {
    if (core_id < num_cores)
        processing_units[core_id].execute();
}

void GPUManager::dispatch_frame() {
    unsigned hw = (num_threads > 0) ? (unsigned)num_threads
                                    : std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;

    for (size_t start = 0; start < num_cores; start += hw) {
        size_t end = std::min(start + (size_t)hw, num_cores);
        std::vector<std::future<void>> batch;
        batch.reserve(end - start);
        for (size_t i = start; i < end; ++i)
            batch.push_back(std::async(std::launch::async, run_core, &processing_units[i]));
        for (auto& f : batch) f.get();
    }

    swap_buffers();
}

void GPUManager::swap_buffers() {
    std::copy(back_buffer.memory.begin(),
              back_buffer.memory.begin() + (ptrdiff_t)total_pixels,
              front_buffer.memory.begin());
    std::fill(back_buffer.memory.begin(),
              back_buffer.memory.begin() + (ptrdiff_t)total_pixels, 0u);
}

const std::vector<uint32_t>& GPUManager::get_pixels() const {
    return front_buffer.memory;
}
