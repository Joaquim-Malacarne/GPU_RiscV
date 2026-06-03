# GPU-V — Emulador de GPU RISC-V

**Projeto acadêmico — Universidade Católica de Santos**

Emulador de GPU que simula **núcleos RISC-V RV32I+M** em paralelo para renderizar um cubo 3D rotacionando em tempo real. Toda a matemática gráfica (rotação, projeção perspectiva, rasterização Bresenham) é executada pelos próprios núcleos RISC-V emulados; a CPU do host atua apenas como despachante.

---

## Demonstração

```
┌────────────────────────────────────────────────┐
│           GPU-V | Cubo 3D  —  ESC para sair    │
│                                                │
│              /───────────/│                    │
│             /           / │                    │
│            /───────────/  │                    │
│            │           │  /                    │
│            │     ○     │ /                     │
│            │           │/                      │
│            └───────────┘                       │
│                                                │
│  1 thread tela SDL2  +  7 threads spin 100%    │
└────────────────────────────────────────────────┘
```

---

## Requisitos

| Dependência | Instalação (Ubuntu/Debian)         |
|-------------|-------------------------------------|
| g++ ≥ 7     | `sudo apt install build-essential`  |
| SDL2        | `sudo apt install libsdl2-dev`      |
| pthread     | incluído no build-essential         |

---

## Compilar e rodar

```bash
# Clone o repositório
git clone https://github.com/Joaquim-Malacarne/GPU_RiscV.git
cd GPU_RiscV

# Compilar o renderizador
make

# Rodar
./gpu_v

# Pressione ESC ou feche a janela para sair
```

```bash
# Recompilar do zero
make clean && make

# Compilar e rodar o benchmark de performance
make benchmark
./benchmark           # suite completa  (~2 min)
./benchmark --quick   # suite rápida    (~40 s)
```

---

## Configuração (`config.h`)

Todos os parâmetros são `constexpr` — edite e recompile para aplicar.

| Variável        | Padrão | Descrição                                                    |
|-----------------|--------|--------------------------------------------------------------|
| `FB_W` / `FB_H` | 1000   | Resolução do framebuffer em pixels                           |
| `SCALE`         | 1      | Fator de escala inicial da janela SDL (redimensionável)      |
| `NUM_CORES`     | 14     | Número de núcleos RV32I emulados                             |
| `COMPUTE_THREADS` | 7   | Threads de cálculo (spin loop — aparecem a 100% no `htop`)  |
| `CUBE_SCALE`    | 0.10   | Tamanho do cubo na tela (0.1 = pequeno, 0.9 = tela cheia)   |
| `ROT_STEP`      | 10     | Passos de ângulo por frame no eixo Y (LUT de 256 entradas)  |
| `ROT_X_RATIO`   | 154    | Velocidade do eixo X em 256avos da velocidade Y (0 = fixo)  |

---

## Arquitetura

### Modelo de threads

```
┌─────────────────────────────────────────────────────────┐
│ Thread principal (main)                                 │
│   └─ gpu.run_continuous()  ← bloqueia aqui             │
│                                                         │
│ Thread da tela (1×)                                     │
│   └─ SDL2: poll_events + draw  ← condvar 16 ms timeout │
│                                                         │
│ Threads de cálculo (COMPUTE_THREADS × — spin 100% CPU) │
│   Fase 0 │ thread 0: geometry shader (núcleo 0)         │
│   ───────┤ [SpinBarrier barrier_geom]                   │
│   Fase 1 │ todas: fragment shaders (stride round-robin) │
│   ───────┤ [SpinBarrier barrier_frag]                   │
│   Fase 2 │ thread 0: swap buffers + publica frame       │
│          └ [SpinBarrier barrier_pub]  → repete          │
└─────────────────────────────────────────────────────────┘
```

As threads de cálculo **nunca dormem** — usam `SpinBarrier` com `fetch_add` atômico e `__builtin_ia32_pause()` (PAUSE), mantendo 100% de utilização de CPU visível no `top`/`htop`.

### Pipeline de renderização por frame

```
angle_idx ──► Geometry Shader (núcleo 0, RV32I)
               │  rotação Y+X em ponto fixo Q8.8
               │  projeção perspectiva
               │  rasterização Bresenham (12 arestas)
               ▼
           edge mask [N, 2N) na VRAM
               │
               ├──► Fragment Shader ×NUM_CORES (paralelo)
               │    cada núcleo: lê mask → pinta azul ou preto
               │    distribução: núcleo i processa pixels [i·ppc, (i+1)·ppc)
               ▼
           color buffer [0, N) na VRAM
               │
               ▼
           SharedFrame ──► SDL2 (thread da tela)
```

### Layout da VRAM

```
Endereço    Conteúdo
[0,      N)   color buffer (fragment shader escreve RGBA)
[N,     2N)   edge mask    (geometry shader escreve 0/1)
[2N +  0]     angle_idx    (CPU escreve 1× por frame, 0–255)
[2N +  1]     sin_lut[256] (Q8.8, inicializado 1× no boot)
[2N +257]     cos_lut[256] (Q8.8, inicializado 1× no boot)
[2N +513]     cube_verts[8×3] (coordenadas Q8.8)
[2N +537]     screen_verts[8×2] (pixels inteiros, geometry escreve)
```

---

## Estrutura de arquivos

| Arquivo              | Função                                                  |
|----------------------|---------------------------------------------------------|
| `config.h`           | Constantes configuráveis (resolução, threads, cubo)     |
| `vram_layout.h`      | Macros de offset na VRAM                                |
| `RV32ICore.h/.cpp`   | Emulador de núcleo RV32I+M (fetch/decode/execute)       |
| `RV32Asm.h`          | Mini-montador inline — gera opcodes RV32I em C++        |
| `VRAM.h`             | Memória linear da GPU (vector de uint32_t)              |
| `shared_frame.h`     | Buffer compartilhado entre GPU e thread da tela         |
| `gpu.h/.cpp`         | GPUManager: SpinBarrier, worker loop, 3 fases           |
| `geom_shader.h/.cpp` | Geometry shader: ~541 instruções RV32I geradas em C++   |
| `frag_shader.h/.cpp` | Fragment shader: ~18 instruções RV32I por núcleo        |
| `janela.h/.cpp`      | Janela SDL2 com FPS no título e resize                  |
| `main.cpp`           | Inicialização, thread da tela, chamada a run_continuous |
| `benchmark.cpp`      | Benchmark headless: testa cores × threads × resolução   |
| `explicacao.md`      | Documentação técnica completa (2000+ linhas)            |

---

## Instruções RV32I+M implementadas

**RV32I base:** `LUI`, `AUIPC`, `JAL`, `JALR`, `BEQ`, `BNE`, `BLT`, `BGE`, `BLTU`, `BGEU`, `LB`, `LH`, `LW`, `LBU`, `LHU`, `SB`, `SH`, `SW`, `ADDI`, `SLTI`, `SLTIU`, `XORI`, `ORI`, `ANDI`, `SLLI`, `SRLI`, `SRAI`, `ADD`, `SUB`, `SLL`, `SLT`, `SLTU`, `XOR`, `SRL`, `SRA`, `OR`, `AND`, `ECALL`

**Extensão M:** `MUL`, `MULH`, `MULHSU`, `MULHU`, `DIV`, `DIVU`, `REM`, `REMU`

---

## Benchmark

O arquivo `benchmark.cpp` testa automaticamente múltiplas combinações e gera um relatório:

```
./benchmark --quick

[1/24] 14 cores /  1 thr  1000x1000  ...  14.9 FPS   14.9 Mpix/s
[2/24] 14 cores /  2 thr  1000x1000  ...  15.7 FPS   15.7 Mpix/s
[3/24] 14 cores /  4 thr  1000x1000  ...  20.5 FPS   20.5 Mpix/s
...
```

Saída: `benchmark_report.txt` (análise por seção) e `benchmark_results.csv` (dados brutos).

**Seções do benchmark:**
1. **Escalabilidade de threads** — varia COMPUTE_THREADS com cores fixos
2. **Escalabilidade de núcleos** — varia NUM_CORES com threads fixas
3. **Impacto da resolução** — varia FB_W×FB_H
4. **Grade cores × threads** — produto cartesiano para encontrar a configuração ótima

---

## Documentação técnica

[`explicacao.md`](explicacao.md) contém documentação detalhada de todos os módulos, incluindo:

- Análise linha-a-linha do SpinBarrier e por que mantém 100% de CPU
- Layout completo da VRAM com justificativas de design
- Explicação dos shaders em assembly RV32I anotado
- Correções dos bugs de power-of-2 (MUL vs SLL/SLLI)
- Diagrama de sequência completo do pipeline por frame
