#ifndef CONFIG_H
#define CONFIG_H

// ── Framebuffer ───────────────────────────────────────────────────────────────
constexpr int FB_W  = 128;   // largura em pixels
constexpr int FB_H  = 128;   // altura em pixels
constexpr int SCALE = 5;     // fator de escala da janela SDL

// ── GPU ───────────────────────────────────────────────────────────────────────
constexpr int NUM_CORES = 64; // número de núcleos RV32I

// Número de threads do host usadas para executar os núcleos em paralelo.
// 0  = detectar automaticamente (hardware_concurrency)
// >0 = usar exatamente esse número de threads
constexpr int NUM_THREADS = 0;

#endif // CONFIG_H
