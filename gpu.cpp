#include "gpu.h"
#include "vram_layout.h"
#include <algorithm>

// Instrução de pausa da CPU: reduz contenção no barramento de cache durante
// o spin sem "dormir" — mantém 100% de uso de CPU no top/htop.
#if defined(__x86_64__) || defined(_M_X64)
#  define CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(__arm__)
#  define CPU_RELAX() __asm__ volatile("yield" ::: "memory")
#else
#  define CPU_RELAX() ((void)0)
#endif

// ── SpinBarrier ──────────────────────────────────────────────────────────────
//
// Barreira de spin reutilizável baseada em geração. O último thread a chegar
// libera todos os outros sem usar mutex/condvar — puro busy-wait.

void GPUManager::SpinBarrier::arrive_and_wait() {
    int g = gen.load(std::memory_order_acquire);
    if (count.fetch_add(1, std::memory_order_acq_rel) + 1 == n) {
        count.store(0, std::memory_order_relaxed);
        gen.fetch_add(1, std::memory_order_release);  // libera os spinners
    } else {
        while (gen.load(std::memory_order_acquire) == g)
            CPU_RELAX();
    }
}

// ── GPUManager ───────────────────────────────────────────────────────────────

GPUManager::GPUManager(size_t cores, size_t pixels)
    : num_cores(cores), total_pixels(pixels),
      back_buffer(vl_total_size(pixels)), front_buffer(pixels),
      barrier_geom_(COMPUTE_THREADS),
      barrier_frag_(COMPUTE_THREADS),
      barrier_pub_(COMPUTE_THREADS) {
    processing_units.reserve(cores);
    for (size_t i = 0; i < cores; ++i)
        processing_units.emplace_back((uint32_t)i, back_buffer);
}

GPUManager::~GPUManager() {
    running_.store(false, std::memory_order_relaxed);
    for (auto& t : compute_threads_)
        if (t.joinable()) t.join();
}

void GPUManager::set_programs(const std::vector<uint32_t>& geom,
                               const std::vector<uint32_t>& frag) {
    geom_prog_ = geom;
    frag_prog_ = frag;
    // Núcleo 0 começa com geometry; demais já ficam com fragment.
    processing_units[0].load_program(geom_prog_);
    for (size_t i = 1; i < num_cores; ++i)
        processing_units[i].load_program(frag_prog_);
}


// Corpo de cada uma das COMPUTE_THREADS threads de cálculo.
// Spin puro: nenhuma chamada a sleep/wait — 100% de CPU o tempo todo.
void GPUManager::worker(int t, SharedFrame& shared) {
    while (running_.load(std::memory_order_relaxed)) {

        // ── Fase 0: geometry ─────────────────────────────────────────────
        // Apenas a thread líder (t == 0); as demais fazem spin em barrier_geom_.
        if (t == 0) {
            uint32_t idx = angle_idx_.fetch_add((uint32_t)ROT_STEP, std::memory_order_relaxed) & 0xFF;
            back_buffer.memory[vl_angle_addr(total_pixels)] = idx;
            processing_units[0].reset();
            processing_units[0].execute();
            // Troca instruction_memory do núcleo 0 para fragment shader
            processing_units[0].load_program(frag_prog_);
        }
        barrier_geom_.arrive_and_wait();

        // ── Fase 1: fragment (TODAS as threads — ~100% CPU aqui) ─────────
        // Cada thread executa os núcleos atribuídos a ela (stride = COMPUTE_THREADS).
        // reset() é barato: zera PC + regs, não copia instruction_memory.
        for (size_t i = (size_t)t; i < num_cores; i += (size_t)COMPUTE_THREADS) {
            processing_units[i].reset();
            processing_units[i].execute();
        }
        barrier_frag_.arrive_and_wait();

        // ── Fase 2: copy + clear paralelos ──────────────────────────────
        // Cada thread processa sua fatia de [0..N) (framebuffer) e [N..2N)
        // (edge mask). Isso elimina o loop O(N) que era serial no geometry shader.
        {
            size_t stripe = (total_pixels + (size_t)COMPUTE_THREADS - 1) / (size_t)COMPUTE_THREADS;
            size_t beg    = (size_t)t * stripe;
            size_t end    = std::min(beg + stripe, total_pixels);
            // copia framebuffer → front e zera back
            std::copy(back_buffer.memory.begin()  + (ptrdiff_t)beg,
                      back_buffer.memory.begin()  + (ptrdiff_t)end,
                      front_buffer.memory.begin() + (ptrdiff_t)beg);
            std::fill(back_buffer.memory.begin()  + (ptrdiff_t)beg,
                      back_buffer.memory.begin()  + (ptrdiff_t)end, 0u);
            // zera edge mask [N+beg .. N+end)
            std::fill(back_buffer.memory.begin()  + (ptrdiff_t)(total_pixels + beg),
                      back_buffer.memory.begin()  + (ptrdiff_t)(total_pixels + end), 0u);
        }
        barrier_pub_.arrive_and_wait();

        if (t == 0) {
            {
                std::lock_guard<std::mutex> lk(shared.mtx);
                if (shared.stop) {
                    running_.store(false, std::memory_order_release);
                } else {
                    shared.pixels.assign(front_buffer.memory.begin(),
                                         front_buffer.memory.begin() + (ptrdiff_t)total_pixels);
                    shared.ready = true;
                }
            }
            shared.cv.notify_one();
            // Restaura geometry no núcleo 0 para a próxima iteração
            processing_units[0].load_program(geom_prog_);
        }
        barrier_pub_.arrive_and_wait();
    }
}

void GPUManager::run_continuous(SharedFrame& shared) {
    running_.store(true, std::memory_order_relaxed);
    angle_idx_.store(0, std::memory_order_relaxed);

    compute_threads_.reserve(COMPUTE_THREADS);
    for (int t = 0; t < COMPUTE_THREADS; ++t)
        compute_threads_.emplace_back(&GPUManager::worker, this, t, std::ref(shared));

    for (auto& th : compute_threads_) th.join();
    compute_threads_.clear();
}
