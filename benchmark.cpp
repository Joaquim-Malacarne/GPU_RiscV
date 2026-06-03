/*
 * benchmark.cpp — GPU-V Performance Benchmark
 * Universidade Católica de Santos
 *
 * Testa múltiplas combinações de NUM_CORES, COMPUTE_THREADS e resolução
 * em modo headless (sem SDL2) e gera tabelas + relatório detalhado.
 *
 * Compilar:
 *   make benchmark
 *   # ou:
 *   g++ -std=c++17 -O2 -march=native -o benchmark \
 *       benchmark.cpp RV32ICore.cpp geom_shader.cpp frag_shader.cpp -lpthread
 *
 * Executar:
 *   ./benchmark           — suite completa  (~2 min)
 *   ./benchmark --quick   — suite rápida    (~40 s)
 *
 * Saída:
 *   stdout               — progresso em tempo real + tabelas por seção
 *   benchmark_report.txt — análise completa com escalabilidade e conclusões
 *   benchmark_results.csv— dados brutos (Excel / LibreOffice / Python/pandas)
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "config.h"
#include "VRAM.h"
#include "RV32ICore.h"
#include "vram_layout.h"
#include "geom_shader.h"
#include "frag_shader.h"

// ── CPU pause hint ─────────────────────────────────────────────────────────
#if defined(__x86_64__) || defined(_M_X64)
#  define CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(__arm__)
#  define CPU_RELAX() __asm__ volatile("yield" ::: "memory")
#else
#  define CPU_RELAX() ((void)0)
#endif

// ══════════════════════════════════════════════════════════════════════════
// SpinBarrier — n configurável em runtime (mesmo algoritmo de gpu.cpp)
// ══════════════════════════════════════════════════════════════════════════
struct SpinBarrier {
    const int        n;
    std::atomic<int> count{0};
    std::atomic<int> gen{0};

    explicit SpinBarrier(int n_) : n(n_) {}

    void arrive_and_wait() {
        int g = gen.load(std::memory_order_acquire);
        if (count.fetch_add(1, std::memory_order_acq_rel) + 1 == n) {
            count.store(0, std::memory_order_relaxed);
            gen.fetch_add(1, std::memory_order_release);
        } else {
            while (gen.load(std::memory_order_acquire) == g)
                CPU_RELAX();
        }
    }
};

// ══════════════════════════════════════════════════════════════════════════
// BenchmarkGPU — motor GPU headless com cores/threads configuráveis em runtime
// ══════════════════════════════════════════════════════════════════════════
class BenchmarkGPU {
    const int    num_cores_, num_threads_;
    const size_t total_pixels_;

    VRAM back_buf_, front_buf_;
    std::vector<RV32ICore>   cores_;
    std::vector<uint32_t>    geom_prog_, frag_prog_;

    SpinBarrier bar_geom_, bar_frag_, bar_pub_;

    std::atomic<bool>        running_{false};
    std::atomic<uint64_t>    frame_count_{0};
    std::atomic<uint32_t>    angle_idx_{0};
    std::vector<std::thread> workers_;

    void swap_buffers() {
        std::copy(back_buf_.memory.begin(),
                  back_buf_.memory.begin() + (ptrdiff_t)total_pixels_,
                  front_buf_.memory.begin());
        std::fill(back_buf_.memory.begin(),
                  back_buf_.memory.begin() + (ptrdiff_t)total_pixels_, 0u);
    }

    void worker(int t) {
        while (running_.load(std::memory_order_relaxed)) {
            // Fase 0 — geometry (só thread líder)
            if (t == 0) {
                uint32_t idx = angle_idx_.fetch_add(
                    (uint32_t)ROT_STEP, std::memory_order_relaxed) & 0xFFu;
                back_buf_.memory[vl_angle_addr(total_pixels_)] = idx;
                cores_[0].reset();
                cores_[0].execute();
                cores_[0].load_program(frag_prog_);
            }
            bar_geom_.arrive_and_wait();

            // Fase 1 — fragment (todas as threads, stride = num_threads_)
            for (int i = t; i < num_cores_; i += num_threads_) {
                cores_[i].reset();
                cores_[i].execute();
            }
            bar_frag_.arrive_and_wait();

            // Fase 2 — publicação (só thread líder)
            if (t == 0) {
                swap_buffers();
                frame_count_.fetch_add(1, std::memory_order_relaxed);
                cores_[0].load_program(geom_prog_);
            }
            bar_pub_.arrive_and_wait();
        }
    }

public:
    size_t geom_instr_count = 0;
    size_t frag_instr_count = 0;

    BenchmarkGPU(int cores, int threads, int W, int H)
        : num_cores_(cores), num_threads_(threads),
          total_pixels_((size_t)W * (size_t)H),
          back_buf_(vl_total_size(total_pixels_)),
          front_buf_(total_pixels_),
          bar_geom_(threads), bar_frag_(threads), bar_pub_(threads)
    {
        cores_.reserve((size_t)cores);
        for (int i = 0; i < cores; ++i)
            cores_.emplace_back((uint32_t)i, back_buf_);

        init_geometry_vram(back_buf_, total_pixels_);
        geom_prog_ = build_geometry_shader(
            (uint32_t)total_pixels_, (uint32_t)W, (uint32_t)H);
        frag_prog_ = build_fragment_shader(
            (uint32_t)cores, (uint32_t)total_pixels_);

        geom_instr_count = geom_prog_.size();
        frag_instr_count = frag_prog_.size();

        cores_[0].load_program(geom_prog_);
        for (int i = 1; i < cores; ++i)
            cores_[i].load_program(frag_prog_);
    }

    // Roda warmup_ms de aquecimento + measure_ms de medição real.
    // Retorna {frames_contados, segundos_da_janela_de_medição}.
    std::pair<uint64_t, double> run(int warmup_ms, int measure_ms) {
        running_.store(true,  std::memory_order_relaxed);
        frame_count_.store(0, std::memory_order_relaxed);
        angle_idx_.store(0,   std::memory_order_relaxed);

        workers_.reserve((size_t)num_threads_);
        for (int t = 0; t < num_threads_; ++t)
            workers_.emplace_back(&BenchmarkGPU::worker, this, t);

        // aquecimento: CPU frequency ramp-up, branch predictor, caches
        std::this_thread::sleep_for(std::chrono::milliseconds(warmup_ms));

        frame_count_.store(0, std::memory_order_relaxed);
        auto t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(measure_ms));
        running_.store(false, std::memory_order_relaxed);

        for (auto& th : workers_) th.join();
        workers_.clear();

        uint64_t f = frame_count_.load(std::memory_order_relaxed);
        double   e = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - t0).count();
        return {f, e};
    }
};

// ══════════════════════════════════════════════════════════════════════════
// Estruturas de configuração e resultado
// ══════════════════════════════════════════════════════════════════════════

struct TestConfig { int cores, threads, W, H; std::string section; };

struct BenchResult {
    TestConfig cfg;
    uint64_t   frames;
    double     elapsed_s, fps, mpix_s;
    size_t     geom_instr, frag_instr;
};

// ── Matriz de testes ───────────────────────────────────────────────────────
//
// 4 seções independentes que isolam cada variável:
//   1 — threads  : varia COMPUTE_THREADS, fixa cores=14, res=1000×1000
//   2 — núcleos  : varia NUM_CORES, fixa threads=min(cores,7), res=1000×1000
//   3 — resolução: varia W×H, fixa cores=14, threads=7
//   4 — grade    : produto cartesiano cores × threads (1000×1000)
//
static const std::vector<TestConfig> TESTS = {
    // Seção 1
    {14,  1, 1000, 1000, "1_threads"},
    {14,  2, 1000, 1000, "1_threads"},
    {14,  4, 1000, 1000, "1_threads"},
    {14,  7, 1000, 1000, "1_threads"},
    {14, 14, 1000, 1000, "1_threads"},

    // Seção 2
    { 1,  1, 1000, 1000, "2_cores"},
    { 2,  2, 1000, 1000, "2_cores"},
    { 4,  4, 1000, 1000, "2_cores"},
    { 8,  7, 1000, 1000, "2_cores"},
    {14,  7, 1000, 1000, "2_cores"},
    {28,  7, 1000, 1000, "2_cores"},

    // Seção 3
    {14,  7,  256,  256, "3_resolution"},
    {14,  7,  512,  512, "3_resolution"},
    {14,  7, 1000, 1000, "3_resolution"},
    {14,  7, 1920, 1080, "3_resolution"},

    // Seção 4
    { 4,  1, 1000, 1000, "4_grid"},
    { 4,  2, 1000, 1000, "4_grid"},
    { 4,  4, 1000, 1000, "4_grid"},
    { 8,  1, 1000, 1000, "4_grid"},
    { 8,  4, 1000, 1000, "4_grid"},
    { 8,  8, 1000, 1000, "4_grid"},
    {16,  2, 1000, 1000, "4_grid"},
    {16,  8, 1000, 1000, "4_grid"},
    {16, 16, 1000, 1000, "4_grid"},
};

// ══════════════════════════════════════════════════════════════════════════
// Utilitários de formatação
// ══════════════════════════════════════════════════════════════════════════

static std::string res_str(int W, int H) {
    return std::to_string(W) + "x" + std::to_string(H);
}

static std::string cpu_name() {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("model name", 0) == 0) {
            auto p = line.find(':');
            if (p != std::string::npos) return line.substr(p + 2);
        }
    return "n/a";
}

static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

static std::string ftos(double v, int prec = 1) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(prec) << v;
    return o.str();
}

// Uma linha da tabela: cores | threads | res | frames | FPS | Mpix/s [| eff%]
static std::string fmt_row(const BenchResult& r, double base_fps = -1.0) {
    std::ostringstream o;
    o << std::setw(6)  << r.cfg.cores
      << std::setw(9)  << r.cfg.threads
      << "  " << std::left << std::setw(12) << res_str(r.cfg.W, r.cfg.H) << std::right
      << std::setw(8)  << r.frames
      << std::setw(9)  << std::fixed << std::setprecision(1) << r.fps
      << std::setw(10) << r.mpix_s;
    if (base_fps > 0.0)
        o << "  " << std::setw(7) << std::setprecision(1)
          << (r.fps / base_fps * 100.0) << "%";
    return o.str();
}

static std::string table_header(bool with_eff = false) {
    std::ostringstream o;
    o << std::setw(6)  << "Cores"
      << std::setw(9)  << "Threads"
      << "  " << std::left << std::setw(12) << "Resolucao" << std::right
      << std::setw(8)  << "Frames"
      << std::setw(9)  << "FPS"
      << std::setw(10) << "Mpix/s";
    if (with_eff)
        o << "  " << std::setw(9) << "Eficiencia";
    return o.str();
}

static std::string sep(int n = 70, char c = '-') { return std::string((size_t)n, c); }

// ══════════════════════════════════════════════════════════════════════════
// Geração do relatório completo
// ══════════════════════════════════════════════════════════════════════════

static void write_report(const std::vector<BenchResult>& all,
                          int warmup_ms, int measure_ms) {

    std::ofstream rep("benchmark_report.txt");
    std::ofstream csv("benchmark_results.csv");

    // helper: filtra resultados por seção
    auto sec = [&](const std::string& tag) {
        std::vector<const BenchResult*> v;
        for (const auto& r : all)
            if (r.cfg.section == tag) v.push_back(&r);
        return v;
    };

    // helper: escreve em arquivo e no console simultaneamente
    auto emit = [&](const std::string& s) {
        std::cout << s;
        rep       << s;
    };

    // ── CSV ───────────────────────────────────────────────────────────────
    csv << "section,cores,threads,W,H,pixels,frames,elapsed_s,"
           "fps,mpix_s,geom_instr,frag_instr,total_instr_per_frame\n";
    for (const auto& r : all) {
        size_t total_instr = r.geom_instr + r.frag_instr * (size_t)r.cfg.cores;
        csv << r.cfg.section << ","
            << r.cfg.cores   << ","
            << r.cfg.threads << ","
            << r.cfg.W       << ","
            << r.cfg.H       << ","
            << ((size_t)r.cfg.W * (size_t)r.cfg.H) << ","
            << r.frames      << ","
            << std::fixed << std::setprecision(4) << r.elapsed_s << ","
            << std::setprecision(2) << r.fps  << ","
            << std::setprecision(2) << r.mpix_s << ","
            << r.geom_instr  << ","
            << r.frag_instr  << ","
            << total_instr   << "\n";
    }

    // ── Cabeçalho do relatório ─────────────────────────────────────────────
    emit(sep(70, '=') + "\n");
    emit("  GPU-V BENCHMARK -- Relatorio Completo\n");
    emit(sep(70, '=') + "\n");
    emit("  CPU        : " + cpu_name() + "\n");
    emit("  Threads HW : " + std::to_string(std::thread::hardware_concurrency()) + "\n");
    emit("  Data       : " + timestamp() + "\n");
    emit("  Aquecimento: " + std::to_string(warmup_ms) + " ms por teste\n");
    emit("  Medicao    : " + std::to_string(measure_ms) + " ms por teste\n");
    emit("  config.h   : CUBE_SCALE=" + ftos(CUBE_SCALE, 2)
         + "  ROT_STEP=" + std::to_string(ROT_STEP)
         + "  ROT_X_RATIO=" + std::to_string(ROT_X_RATIO) + "\n");
    emit(sep(70, '=') + "\n\n");

    // ══════════════════════════════════════════════════════════════════════
    // SEÇÃO 1 — Escalabilidade de Threads
    // ══════════════════════════════════════════════════════════════════════
    {
        emit(sep(70, '=') + "\n");
        emit("SECAO 1 -- Escalabilidade de Threads\n");
        emit("  Fixo: 14 nucleos RV32I, resolucao 1000x1000\n");
        emit("  Varia: numero de COMPUTE_THREADS (1 a 14)\n");
        emit(sep(70, '-') + "\n");
        emit(table_header(true) + "\n");
        emit(sep(70, '-') + "\n");

        auto rows = sec("1_threads");
        double base_fps = rows.empty() ? 1.0 : rows[0]->fps;

        for (const auto* r : rows)
            emit(fmt_row(*r, base_fps) + "\n");

        emit(sep(70, '-') + "\n");

        if (rows.size() >= 2) {
            double fps1 = rows.front()->fps;
            double fpsN = rows.back()->fps;
            int    nT   = rows.back()->cfg.threads;
            double eff  = (fps1 > 0.0) ? fpsN / (fps1 * nT) * 100.0 : 0.0;

            emit("\nAnalise:\n");
            emit("  Speedup total (1 -> " + std::to_string(nT) + " threads): "
                 + ftos(fps1 > 0.0 ? fpsN / fps1 : 0.0, 2) + "x\n");
            emit("  Eficiencia paralela: " + ftos(eff, 1) + "%"
                 " (ideal = 100%)\n");

            // ponto de retorno decrescente
            double prev = fps1;
            for (size_t i = 1; i < rows.size(); ++i) {
                double gain = (prev > 0.0) ? rows[i]->fps / prev : 0.0;
                if (gain < 1.15 && rows[i]->cfg.threads > 1) {
                    emit("  Rendimento decrescente a partir de "
                         + std::to_string(rows[i]->cfg.threads)
                         + " threads (ganho marginal: "
                         + ftos((gain - 1.0) * 100.0, 1) + "%)\n");
                    break;
                }
                prev = rows[i]->fps;
            }

            // explica por que a eficiência não é 100%
            emit("\n  Por que a eficiencia nao e 100%:\n");
            emit("    - Fase 0 (geometry) e serial: apenas thread 0 trabalha\n");
            emit("      enquanto as outras " + std::to_string(nT-1)
                 + " threads ficam em spin na barrier_geom\n");
            emit("    - geometry_shader tem loop de limpeza O(N): N="
                 + std::to_string(rows[0]->cfg.W * rows[0]->cfg.H)
                 + " iteracoes por frame\n");
            emit("    - Overhead de spin na barrier (~3 ciclos/checagem)\n");
            emit("    - Lei de Amdahl: a fração serial limita o speedup maximo\n");
        }
        emit("\n");
    }

    // ══════════════════════════════════════════════════════════════════════
    // SEÇÃO 2 — Escalabilidade de Núcleos
    // ══════════════════════════════════════════════════════════════════════
    {
        emit(sep(70, '=') + "\n");
        emit("SECAO 2 -- Escalabilidade de Nucleos RV32I\n");
        emit("  Fixo: resolucao 1000x1000  threads = min(cores, 7)\n");
        emit("  Varia: NUM_CORES (1, 2, 4, 8, 14, 28)\n");
        emit(sep(70, '-') + "\n");
        emit(table_header(true) + "\n");
        emit(sep(70, '-') + "\n");

        auto rows = sec("2_cores");
        double base_fps = rows.empty() ? 1.0 : rows[0]->fps;

        for (const auto* r : rows)
            emit(fmt_row(*r, base_fps) + "\n");

        emit(sep(70, '-') + "\n");

        emit("\nAnalise:\n");
        emit("  O trabalho do fragment shader e fixo: N="
             + std::to_string(rows.empty() ? 0 : rows[0]->cfg.W * rows[0]->cfg.H)
             + " pixels/frame\n");
        emit("  Com mais nucleos, cada nucleo processa menos pixels (N/cores).\n");
        emit("  O custo de reset() cresce linearmente com cores (32 regs x cores).\n");
        emit("  A fase 0 (geometry) tem complexidade O(N) no loop de limpeza,\n");
        emit("  independente do numero de nucleos.\n");

        if (rows.size() >= 2) {
            double fps1 = rows.front()->fps;
            double fpsN = rows.back()->fps;
            emit("  Speedup (1 -> " + std::to_string(rows.back()->cfg.cores)
                 + " nucleos): " + ftos(fps1 > 0.0 ? fpsN / fps1 : 0.0, 2) + "x\n");
        }
        emit("\n");
    }

    // ══════════════════════════════════════════════════════════════════════
    // SEÇÃO 3 — Impacto da Resolução
    // ══════════════════════════════════════════════════════════════════════
    {
        emit(sep(70, '=') + "\n");
        emit("SECAO 3 -- Impacto da Resolucao\n");
        emit("  Fixo: 14 nucleos, 7 threads\n");
        emit("  Varia: resolucao (256x256 ate 1920x1080)\n");
        emit(sep(70, '-') + "\n");

        // tabela especial com coluna de pixels
        std::ostringstream hdr3;
        hdr3 << std::setw(6)  << "Cores"
             << std::setw(9)  << "Threads"
             << "  " << std::left << std::setw(12) << "Resolucao" << std::right
             << std::setw(10) << "Pixels"
             << std::setw(8)  << "Frames"
             << std::setw(9)  << "FPS"
             << std::setw(10) << "Mpix/s";
        emit(hdr3.str() + "\n");
        emit(sep(70, '-') + "\n");

        auto rows = sec("3_resolution");
        for (const auto* r : rows) {
            std::ostringstream row;
            row << std::setw(6)  << r->cfg.cores
                << std::setw(9)  << r->cfg.threads
                << "  " << std::left << std::setw(12) << res_str(r->cfg.W, r->cfg.H) << std::right
                << std::setw(10) << ((size_t)r->cfg.W * (size_t)r->cfg.H)
                << std::setw(8)  << r->frames
                << std::setw(9)  << std::fixed << std::setprecision(1) << r->fps
                << std::setw(10) << r->mpix_s;
            emit(row.str() + "\n");
        }
        emit(sep(70, '-') + "\n");

        emit("\nAnalise:\n");
        emit("  FPS cai com resolucao maior porque:\n");
        emit("    1. Fragment shader: O(N) pixels por frame\n");
        emit("    2. Geometry shader: loop de limpeza O(N) iteracoes\n");
        emit("    3. swap_buffers: std::copy de N palavras de 32 bits\n");
        emit("  O produto FPS x pixels = Mpix/s mede throughput real.\n");
        emit("  Se Mpix/s for constante em todas resolucoes -> pipeline\n");
        emit("  e limitado pelo processamento de pixels (compute-bound).\n");
        emit("  Se Mpix/s cair com resolucao maior -> limitado por\n");
        emit("  memoria/banda ou overhead de limpeza O(N).\n");

        if (rows.size() >= 2) {
            double mpix_first = rows.front()->mpix_s;
            double mpix_last  = rows.back()->mpix_s;
            if (mpix_first > 0.0) {
                double ratio = mpix_last / mpix_first;
                if (ratio < 0.7)
                    emit("  RESULTADO: Mpix/s caiu " + ftos((1.0 - ratio) * 100.0, 0)
                         + "% -> overhead O(N) significativo.\n");
                else
                    emit("  RESULTADO: Mpix/s estavel (" + ftos(ratio * 100.0, 0)
                         + "% do valor em baixa resolucao) -> compute-bound.\n");
            }
        }
        emit("\n");
    }

    // ══════════════════════════════════════════════════════════════════════
    // SEÇÃO 4 — Grade Cores × Threads
    // ══════════════════════════════════════════════════════════════════════
    {
        emit(sep(70, '=') + "\n");
        emit("SECAO 4 -- Grade Cores x Threads (resolucao 1000x1000)\n");
        emit(sep(70, '-') + "\n");
        emit(table_header(false) + "\n");
        emit(sep(70, '-') + "\n");

        auto rows = sec("4_grid");
        for (const auto* r : rows)
            emit(fmt_row(*r) + "\n");
        emit(sep(70, '-') + "\n");

        // grade 2D: encontra melhor config por (cores, threads)
        emit("\nMelhor FPS por numero de threads:\n");
        for (int t : {1, 2, 4, 8, 16}) {
            const BenchResult* best = nullptr;
            for (const auto* r : rows) {
                if (r->cfg.threads == t)
                    if (!best || r->fps > best->fps) best = r;
            }
            if (best) {
                emit("  threads=" + std::to_string(t) + ": melhor = "
                     + std::to_string(best->cfg.cores) + " cores => "
                     + ftos(best->fps, 1) + " FPS\n");
            }
        }
        emit("\n");
    }

    // ══════════════════════════════════════════════════════════════════════
    // RESUMO GERAL
    // ══════════════════════════════════════════════════════════════════════
    {
        emit(sep(70, '=') + "\n");
        emit("RESUMO GERAL\n");
        emit(sep(70, '=') + "\n\n");

        // Top 5 por FPS
        std::vector<const BenchResult*> sorted;
        for (const auto& r : all) sorted.push_back(&r);
        std::sort(sorted.begin(), sorted.end(),
                  [](const BenchResult* a, const BenchResult* b) {
                      return a->fps > b->fps;
                  });

        emit("Top 5 configuracoes por FPS:\n");
        emit(sep(70, '-') + "\n");
        emit(table_header(false) + "\n");
        emit(sep(70, '-') + "\n");
        int shown = 0;
        for (const auto* r : sorted) {
            emit(fmt_row(*r) + "\n");
            if (++shown >= 5) break;
        }
        emit(sep(70, '-') + "\n\n");

        // Top 5 por Mpix/s
        std::sort(sorted.begin(), sorted.end(),
                  [](const BenchResult* a, const BenchResult* b) {
                      return a->mpix_s > b->mpix_s;
                  });

        emit("Top 5 configuracoes por Mpix/s (throughput de pixels):\n");
        emit(sep(70, '-') + "\n");
        emit(table_header(false) + "\n");
        emit(sep(70, '-') + "\n");
        shown = 0;
        for (const auto* r : sorted) {
            emit(fmt_row(*r) + "\n");
            if (++shown >= 5) break;
        }
        emit(sep(70, '-') + "\n\n");

        // Recomendação para config.h
        const BenchResult* best_fps = sorted[0];
        // re-sort by fps
        std::sort(sorted.begin(), sorted.end(),
                  [](const BenchResult* a, const BenchResult* b) {
                      return a->fps > b->fps;
                  });
        best_fps = sorted[0];

        emit("Recomendacao para config.h com base neste hardware:\n");
        emit("  NUM_CORES       = " + std::to_string(best_fps->cfg.cores) + "\n");
        emit("  COMPUTE_THREADS = " + std::to_string(best_fps->cfg.threads) + "\n");
        emit("  (configuracao que atingiu " + ftos(best_fps->fps, 1)
             + " FPS em " + res_str(best_fps->cfg.W, best_fps->cfg.H) + ")\n\n");

        // Overhead da fase serial
        emit("Nota sobre escalabilidade (Lei de Amdahl):\n");
        emit("  A fase 0 (geometry shader) e serial — so thread 0 trabalha.\n");
        emit("  Ela inclui: rotacao de 8 vertices + 12 arestas Bresenham +\n");
        emit("  loop de limpeza O(N). Para 1000x1000, sao ~1M iteracoes do\n");
        emit("  loop de limpeza so. Isso cria um teto de speedup independente\n");
        emit("  de quantas threads de calculo sao adicionadas.\n\n");

        emit("Legenda de colunas:\n");
        emit("  FPS       = frames por segundo (rendering headless, sem SDL)\n");
        emit("  Mpix/s    = megapixels por segundo = FPS x W x H / 1e6\n");
        emit("  Eficiencia= FPS_N / (FPS_1 x N_threads) x 100%\n");
        emit("              100% = speedup linear perfeito (teorico)\n\n");

        emit("Arquivos gerados:\n");
        emit("  benchmark_report.txt  — este relatorio\n");
        emit("  benchmark_results.csv — dados brutos (abrir em planilha)\n\n");

        emit(sep(70, '=') + "\n");
    }
}

// ══════════════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    bool quick = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--quick") quick = true;

    const int warmup_ms  = 500;
    const int measure_ms = quick ? 1000 : 3000;
    const int n_tests    = (int)TESTS.size();

    std::cout << "\n" << sep(70, '=') << "\n";
    std::cout << "  GPU-V Benchmark  ["
              << (quick ? "modo rapido" : "modo completo") << "]\n";
    std::cout << "  CPU: " << cpu_name() << "\n";
    std::cout << "  Threads HW: " << std::thread::hardware_concurrency() << "\n";
    std::cout << "  Medicao: " << measure_ms << " ms/teste  "
              << "Total de testes: " << n_tests << "\n";
    std::cout << sep(70, '=') << "\n\n";

    std::vector<BenchResult> results;
    results.reserve((size_t)n_tests);

    for (int idx = 0; idx < n_tests; ++idx) {
        const auto& cfg = TESTS[(size_t)idx];

        std::cout << "[" << std::setw(2) << (idx + 1) << "/" << n_tests << "] "
                  << std::setw(2) << cfg.cores    << " cores / "
                  << std::setw(2) << cfg.threads  << " thr  "
                  << std::left << std::setw(11) << res_str(cfg.W, cfg.H) << std::right
                  << " ... " << std::flush;

        BenchmarkGPU gpu(cfg.cores, cfg.threads, cfg.W, cfg.H);
        auto [frames, elapsed] = gpu.run(warmup_ms, measure_ms);

        BenchResult r;
        r.cfg        = cfg;
        r.frames     = frames;
        r.elapsed_s  = elapsed;
        r.fps        = (elapsed > 0.0) ? (double)frames / elapsed : 0.0;
        r.mpix_s     = r.fps * (double)(cfg.W * cfg.H) / 1e6;
        r.geom_instr = gpu.geom_instr_count;
        r.frag_instr = gpu.frag_instr_count;
        results.push_back(r);

        std::cout << std::fixed << std::setprecision(1)
                  << std::setw(7) << r.fps << " FPS"
                  << std::setw(8) << r.mpix_s << " Mpix/s"
                  << "  [geom=" << r.geom_instr << " frag=" << r.frag_instr << " instr]\n";
    }

    std::cout << "\nGerando relatorio...\n\n";
    write_report(results, warmup_ms, measure_ms);

    std::cout << "\nArquivos gerados:\n";
    std::cout << "  benchmark_report.txt\n";
    std::cout << "  benchmark_results.csv\n\n";
    return 0;
}
