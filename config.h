#ifndef CONFIG_H
#define CONFIG_H

// ── Framebuffer ───────────────────────────────────────────────────────────────
constexpr int FB_W  = 1000; // largura em pixels
constexpr int FB_H  = 1000; // altura em pixels
constexpr int SCALE = 1;    // fator de escala inicial da janela SDL

// ── GPU ───────────────────────────────────────────────────────────────────────
constexpr int NUM_CORES       = 14; // número de núcleos RV32I
constexpr int COMPUTE_THREADS = 7; // threads de cálculo (pool fixo, separado da tela)

// ── Tamanho do cubo ───────────────────────────────────────────────────────────
// Fração de FB_W/FB_H que o cubo ocupa na tela (projeção perspectiva).
//   0.2 → pequeno   0.45 → padrão   0.9 → quase toda a tela
constexpr float CUBE_SCALE = 0.1f;

// ── Velocidade de rotação do cubo ─────────────────────────────────────────────
// ROT_STEP: passos de ângulo por frame no eixo Y (LUT tem 256 entradas = 360°)
//   1  → ~1.4°/frame (lento)   4 → ~5.6°/frame   16 → ~22°/frame (rápido)
constexpr int ROT_STEP    = 5;

// ROT_X_RATIO: velocidade do eixo X em relação ao Y, em 256avos (0–255)
//   0   → sem rotação X        128 → 50% da Y        256 → igual à Y
//   154 → ~60% da Y (padrão)
constexpr int ROT_X_RATIO = 154;

#endif // CONFIG_H
