/*
 * GPU-V  —  Emulador de GPU baseado em RISC-V RV32I
 * Universidade Católica de Santos
 *
 * Correções e melhorias em relação à entrega 1:
 *   1. ISA completa (R, I, S, B, U, J) em RV32ICore.cpp
 *   2. GPUManager::load_program() distribui o código a cada núcleo
 *   3. Thread pool limitado ao hardware concorrente real (evita explosão de threads)
 *   4. Programa shader de exemplo que realmente pinta pixels na VRAM
 *   5. Diagnóstico de saída com estatísticas do frame renderizado
 */

#include <iostream>
#include <vector>
#include <future>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <SDL2/SDL.h>
#include "VRAM.h"
#include "RV32ICore.h"

// ──────────────────────────────────────────────────────────────────────────────
// Montador mínimo de instruções RV32I (para construir o shader em C++)
// ──────────────────────────────────────────────────────────────────────────────
namespace RV32Asm {
    // Tipo-R
    static uint32_t ADD (int rd,int rs1,int rs2){return (rs2<<20)|(rs1<<15)|(rd<<7)|0x33;}
    static uint32_t SUB (int rd,int rs1,int rs2){return (0x20<<25)|(rs2<<20)|(rs1<<15)|(rd<<7)|0x33;}
    static uint32_t SLL (int rd,int rs1,int rs2){return (rs2<<20)|(rs1<<15)|(0x1<<12)|(rd<<7)|0x33;}
    static uint32_t AND (int rd,int rs1,int rs2){return (rs2<<20)|(rs1<<15)|(0x7<<12)|(rd<<7)|0x33;}
    static uint32_t OR  (int rd,int rs1,int rs2){return (rs2<<20)|(rs1<<15)|(0x6<<12)|(rd<<7)|0x33;}
    // Tipo-I
    static uint32_t ADDI(int rd,int rs1,int imm){return ((imm&0xFFF)<<20)|(rs1<<15)|(rd<<7)|0x13;}
    static uint32_t ANDI(int rd,int rs1,int imm){return ((imm&0xFFF)<<20)|(rs1<<15)|(0x7<<12)|(rd<<7)|0x13;}
    static uint32_t SRLI(int rd,int rs1,int shamt){return (shamt<<20)|(rs1<<15)|(0x5<<12)|(rd<<7)|0x13;}
    static uint32_t LW  (int rd,int rs1,int imm){return ((imm&0xFFF)<<20)|(rs1<<15)|(0x2<<12)|(rd<<7)|0x03;}
    // Tipo-U
    static uint32_t LUI (int rd,int imm20){return ((imm20&0xFFFFF)<<12)|(rd<<7)|0x37;}
    // Tipo-S
    static uint32_t SW  (int rs2,int rs1,int imm){
        return (((imm>>5)&0x7F)<<25)|(rs2<<20)|(rs1<<15)|(0x2<<12)|(((imm)&0x1F)<<7)|0x23;}
    // Branch
    static uint32_t BEQ (int rs1,int rs2,int off){
        int o=off;
        return (((o>>12)&1)<<31)|(((o>>5)&0x3F)<<25)|(rs2<<20)|(rs1<<15)|(((o>>1)&0xF)<<8)|(((o>>11)&1)<<7)|0x63;}
    static uint32_t BNE (int rs1,int rs2,int off){
        int o=off;
        return (((o>>12)&1)<<31)|(((o>>5)&0x3F)<<25)|(rs2<<20)|(rs1<<15)|(0x1<<12)|(((o>>1)&0xF)<<8)|(((o>>11)&1)<<7)|0x63;}
    // ECALL (encerra o núcleo)
    static uint32_t ECALL(){return 0x00000073;}
}

// ──────────────────────────────────────────────────────────────────────────────
// Shader: cada núcleo pinta sua fatia de pixels com uma cor derivada do mhartid
//
// Convenção de registradores (ABI RISC-V):
//   x10 (a0) = mhartid  (preenchido pelo construtor RV32ICore)
//   x1       = temporário / ra
//   x5–x7    = t0–t2
//   x28–x31  = t3–t6
//
// Algoritmo por núcleo:
//   pixels_per_core = total_pixels / num_cores
//   base_addr       = mhartid * pixels_per_core
//   color           = gradient baseado no mhartid
//   for i in 0..pixels_per_core: VRAM[base_addr + i] = color
// ──────────────────────────────────────────────────────────────────────────────
static std::vector<uint32_t> build_gradient_shader(uint32_t num_cores,
                                                    uint32_t total_pixels) {
    using namespace RV32Asm;
    // Registradores usados:
    //  x10 = a0  = mhartid (já setado)
    //  x5  = t0  = pixels_per_core
    //  x6  = t1  = base_addr
    //  x7  = t2  = color (RGBA)
    //  x28 = t3  = loop counter (i)
    //  x29 = t4  = current addr
    //  x30 = t5  = temporário

    std::vector<uint32_t> prog;

    // t0 = total_pixels / num_cores   (usando LUI + ADDI para carregar constantes)
    uint32_t ppc = total_pixels / num_cores;

    // Carrega ppc em t0 (x5)
    // LUI  t0, ppc_hi  ; ADDI t0, t0, ppc_lo
    uint32_t ppc_hi = (ppc >> 12) & 0xFFFFF;
    int32_t  ppc_lo = ppc & 0xFFF;
    if (ppc_lo >= 2048) { ppc_hi++; ppc_lo -= 4096; }
    prog.push_back(LUI (5, ppc_hi));        // t0 = ppc_hi << 12
    prog.push_back(ADDI(5, 5, ppc_lo));     // t0 += ppc_lo  → t0 = ppc

    // base_addr (t1) = mhartid (a0) * ppc (t0)
    // Multiplicação usando shifts + adds seria longa; como ppc é pot. de 2 → SLL
    // Calculamos log2(ppc) em tempo de montagem:
    uint32_t shift = 0, tmp = ppc;
    while (tmp > 1) { tmp >>= 1; shift++; }

    prog.push_back(SLL(6, 10, 0));          // t1 = a0 (copia mhartid)
    // t1 = a0 << shift  (precisamos colocar shift em um reg temporário)
    prog.push_back(ADDI(30, 0, shift));     // t5 = shift
    prog.push_back(SLL(6, 10, 30));         // t1 = mhartid << shift  = base_addr

    // Azul sólido: 0x0000FFFF  (R=0, G=0, B=255, A=255)
    // LUI carrega 0x00010000; ADDI subtrai 1 → 0x0000FFFF
    prog.push_back(LUI (7, 0x10));               // t2 = 0x00010000
    prog.push_back(ADDI(7,  7, -1));             // t2 = 0x0000FFFF

    // Loop: i (t3=x28) de 0 até ppc
    // current_addr (t4=x29) = t1 (base) + i
    prog.push_back(ADDI(28, 0, 0));              // t3 = 0  (i = 0)

    // loop_start: (offset calculado depois — usamos BNE com forward jump)
    // addr = base_addr + i
    // SW color, addr(x0)  → vram_ref.write_pixel(addr, color)
    // i++
    // BNE i, ppc, loop_start

    (void)prog.size(); // loop_top não usado após refatoração
    prog.push_back(ADD(29, 6, 28));              // t4 = base_addr + i
    prog.push_back(SW (7, 29, 0));              // VRAM[t4] = color
    prog.push_back(ADDI(28, 28, 1));             // i++
    // BNE t3, t0, -12  (volta 3 instruções = -12 bytes)
    prog.push_back(BNE(28, 5, -12));             // if i != ppc goto loop_top

    prog.push_back(ECALL());                     // fim do núcleo

    return prog;
}

// ──────────────────────────────────────────────────────────────────────────────
// GPU Manager
// ──────────────────────────────────────────────────────────────────────────────
class GPUManager {
    size_t num_cores;
    std::vector<RV32ICore> processing_units;
    VRAM back_buffer;   // núcleos escrevem aqui (invisível ao usuário)
    VRAM front_buffer;  // exibido ao usuário após o swap

public:
    GPUManager(size_t cores_count, size_t vram_size)
        : num_cores(cores_count), back_buffer(vram_size), front_buffer(vram_size) {
        processing_units.reserve(cores_count);
        for (size_t i = 0; i < num_cores; ++i)
            processing_units.emplace_back((uint32_t)i, back_buffer);
    }

    // Carrega o mesmo programa em todos os núcleos
    void load_program(const std::vector<uint32_t>& prog) {
        for (auto& core : processing_units)
            core.load_program(prog);
    }

    void dispatch_frame() {
        // Thread pool: usa tantas threads quanto o hardware suporta
        unsigned int hw_threads = std::thread::hardware_concurrency();
        if (hw_threads == 0) hw_threads = 4;

        std::cout << "[GPU Manager] Disparando " << num_cores
                  << " núcleos em " << hw_threads << " thread(s) físicas.\n";

        // Divide os núcleos em batches do tamanho do pool
        size_t batch_size = hw_threads;
        for (size_t base = 0; base < num_cores; base += batch_size) {
            std::vector<std::future<void>> futures;
            size_t end = std::min(base + batch_size, num_cores);
            for (size_t i = base; i < end; ++i) {
                futures.push_back(
                    std::async(std::launch::async,
                               &RV32ICore::execute,
                               &processing_units[i])
                );
            }
            for (auto& f : futures) f.get();
        }

        std::cout << "[GPU Manager] Back Buffer pronto.\n";
        swap_buffers();
    }

    void swap_buffers() {
        // Troca real de ponteiros: front recebe o frame pronto, back é limpo
        std::swap(back_buffer.memory, front_buffer.memory);
        std::fill(back_buffer.memory.begin(), back_buffer.memory.end(), 0u);
        std::cout << "[GPU Manager] Swap efetuado — Front Buffer exibido.\n";
    }

    // Retorna o front buffer (frame completo visível ao usuário)
    const std::vector<uint32_t>& get_pixels() const { return front_buffer.memory; }

    // Diagnóstico: conta pixels não-nulos e mostra alguns valores
    void print_diagnostics(size_t sample_count = 8) const {
        size_t nonzero = 0;
        for (auto px : front_buffer.memory)
            if (px != 0) nonzero++;

        std::cout << "\n=== Diagnóstico de Frame ===\n";
        std::cout << "  Pixels totais : " << front_buffer.size() << "\n";
        std::cout << "  Pixels escritos (≠0): " << nonzero << "\n";
        std::cout << "  Amostra (endereços 0.." << sample_count-1 << "):\n";
        for (size_t i = 0; i < sample_count && i < front_buffer.size(); ++i) {
            uint32_t px = front_buffer.memory[i];
            std::cout << "    [" << std::setw(4) << i << "] "
                      << "RGBA = 0x" << std::hex << std::setw(8)
                      << std::setfill('0') << px << std::dec
                      << std::setfill(' ') << "\n";
        }
        std::cout << "============================\n";
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Visualização SDL2: exibe ANTES (esquerda) e DEPOIS (direita) do frame buffer.
// Layout: cada coluna x = núcleo x, cada linha y = pixel y dentro do núcleo.
// pixel_index = x * fb_height + y  (column-major, cores como colunas)
// ──────────────────────────────────────────────────────────────────────────────
static void visualize_before_after(
        const std::vector<uint32_t>& before,
        const std::vector<uint32_t>& after,
        int fb_width, int fb_height)
{
    const int SCALE  = 10;  // cada pixel do framebuffer → 10×10 px na tela
    const int HEADER = 24;  // altura da faixa de rótulo
    const int SEP    = 4;   // largura do separador central

    int win_w = fb_width * 2 * SCALE + SEP;
    int win_h = fb_height * SCALE + HEADER;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[SDL] Init falhou: " << SDL_GetError() << "\n";
        return;
    }

    SDL_Window* win = SDL_CreateWindow(
        "GPU-V  |  ANTES (esq.)  vs  DEPOIS (dir.)  —  ESC ou fechar para sair",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, SDL_WINDOW_SHOWN);
    if (!win) { SDL_Quit(); return; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) { SDL_DestroyWindow(win); SDL_Quit(); return; }

    // Fundo escuro
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderClear(ren);

    // Desenha um painel (before ou after) na posição offset_x
    auto draw_panel = [&](const std::vector<uint32_t>& buf, int offset_x,
                          uint8_t hr, uint8_t hg, uint8_t hb) {
        // Faixa de rótulo colorida no topo
        SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
        SDL_Rect hdr = { offset_x, 0, fb_width * SCALE, HEADER };
        SDL_RenderFillRect(ren, &hdr);

        // Pixels — mapeamento column-major: coluna = núcleo, linha = pixel do núcleo
        for (int x = 0; x < fb_width; ++x) {
            for (int y = 0; y < fb_height; ++y) {
                uint32_t px = buf[x * fb_height + y];
                uint8_t r = (px >> 24) & 0xFF;
                uint8_t g = (px >> 16) & 0xFF;
                uint8_t b = (px >>  8) & 0xFF;
                SDL_SetRenderDrawColor(ren, r, g, b, 255);
                SDL_Rect rect = { offset_x + x * SCALE, HEADER + y * SCALE, SCALE, SCALE };
                SDL_RenderFillRect(ren, &rect);
            }
        }
    };

    // Painel ANTES — faixa vermelha
    draw_panel(before, 0, 160, 40, 40);

    // Separador branco
    SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    SDL_Rect sep = { fb_width * SCALE, 0, SEP, win_h };
    SDL_RenderFillRect(ren, &sep);

    // Painel DEPOIS — faixa verde
    draw_panel(after, fb_width * SCALE + SEP, 40, 160, 40);

    SDL_RenderPresent(ren);

    std::cout << "\n[Visualizer] Janela aberta — faixa VERMELHA = ANTES | faixa VERDE = DEPOIS\n"
              << "             Pressione ESC ou feche a janela para encerrar.\n";

    SDL_Event e;
    bool running = true;
    while (running) {
        SDL_WaitEvent(&e);
        if (e.type == SDL_QUIT ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
            running = false;
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ──────────────────────────────────────────────────────────────────────────────
int main() {
    // Resolução simulada: 64×16 = 1024 pixels, 1 pixel por núcleo
    const size_t NUM_CORES     = 64;
    const size_t TOTAL_PIXELS  = 1024;   // pixels no framebuffer
    const size_t PIXELS_PER_CORE = TOTAL_PIXELS / NUM_CORES;

    std::cout << "=== GPU-V: Emulador RISC-V RV32I ===\n";
    std::cout << "Núcleos      : " << NUM_CORES << "\n";
    std::cout << "Pixels total : " << TOTAL_PIXELS << "\n";
    std::cout << "Pixels/núcleo: " << PIXELS_PER_CORE << "\n\n";

    GPUManager gpu(NUM_CORES, TOTAL_PIXELS);

    // Snapshot ANTES: VRAM recém-criada (todos zeros = preto)
    std::vector<uint32_t> before_snapshot = gpu.get_pixels();

    // Monta o shader de gradiente e carrega em todos os núcleos
    auto shader = build_gradient_shader((uint32_t)NUM_CORES, (uint32_t)TOTAL_PIXELS);
    std::cout << "Shader compilado: " << shader.size() << " instrução(ões).\n";
    gpu.load_program(shader);

    // Processa o frame
    gpu.dispatch_frame();

    // Snapshot DEPOIS
    std::vector<uint32_t> after_snapshot = gpu.get_pixels();

    // Diagnóstico
    gpu.print_diagnostics(16);

    // Visualização SDL2: janela com ANTES (esq.) e DEPOIS (dir.)
    // Layout: 64 colunas (núcleos) × 16 linhas (pixels por núcleo), escala 10×
    visualize_before_after(before_snapshot, after_snapshot,
                           (int)NUM_CORES, (int)PIXELS_PER_CORE);

    return 0;
}