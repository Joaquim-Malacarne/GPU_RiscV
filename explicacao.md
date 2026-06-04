# GPU-V — Explicação Técnica Ultra-Detalhada

> Este documento explica cada arquivo, cada função e cada linha relevante do projeto, do zero. Não é necessário conhecimento prévio de arquitetura de computadores ou gráficos 3D.

---

## Sumário

1. [O que é este projeto?](#1-o-que-é-este-projeto)
2. [Estrutura de arquivos](#2-estrutura-de-arquivos)
3. [config.h — constantes globais](#3-configh--constantes-globais)
4. [O que é RISC-V?](#4-o-que-é-risc-v)
5. [O que é uma GPU e como ela funciona?](#5-o-que-é-uma-gpu-e-como-ela-funciona)
6. [Visão geral do pipeline](#6-visão-geral-do-pipeline)
7. [VRAM.h — a memória da GPU](#7-vramh--a-memória-da-gpu)
8. [vram_layout.h — mapa de endereços](#8-vram_layouth--mapa-de-endereços)
9. [Aritmética de ponto fixo Q8.8](#9-aritmética-de-ponto-fixo-q88)
10. [Matemática 3D: rotação e projeção](#10-matemática-3d-rotação-e-projeção)
11. [RV32ICore.h — estrutura do núcleo](#11-rv32icoreh--estrutura-do-núcleo)
12. [RV32ICore.cpp — ciclo fetch-decode-execute](#12-rv32icorecpp--ciclo-fetch-decode-execute)
13. [RV32Asm.h — o mini-montador](#13-rv32asmh--o-mini-montador)
14. [geom_shader.cpp — shader de geometria](#14-geom_shadercpp--shader-de-geometria)
15. [frag_shader.cpp — shader de fragmentos](#15-frag_shadercpp--shader-de-fragmentos)
16. [gpu.h e gpu.cpp — o gerenciador de GPU](#16-gph-e-gpucpp--o-gerenciador-de-gpu)
17. [janela.h e janela.cpp — a janela SDL2](#17-janelah-e-janelacpp--a-janela-sdl2)
18. [main.cpp — o loop principal](#18-maincpp--o-loop-principal)
19. [Rasterização: algoritmo de Bresenham](#19-rasterização-algoritmo-de-bresenham)
20. [Fluxo completo de um frame](#20-fluxo-completo-de-um-frame)
21. [Novo modelo de threads — separação tela / cálculo](#21-novo-modelo-de-threads--separação-tela--cálculo)
22. [SharedFrame — buffer produtor-consumidor](#22-sharedframe--o-buffer-produtor-consumidor)
23. [SpinBarrier e 100% de CPU — ultra-detalhado](#23-spinbarrier-e-100-de-utilização-de-cpu--explicação-ultra-detalhada)
24. [RV32ICore::reset() — reinicialização eficiente](#24-rv32icoreresett--reinicialização-eficiente-sem-recarga)
25. [Correção de bugs de resolução](#25-correção-de-bugs-de-resolução--independência-de-potência-de-2)
26. [Controles de config.h — referência completa](#26-controles-de-configh--referência-completa)
27. [Janela redimensionável — SDL_RenderSetLogicalSize](#27-janela-redimensionável--sdl_rendersetlogicalsize)
28. [Benchmark — Metodologia e Objetivo](#28-benchmark--metodologia-e-objetivo)
29. [Seção 1 — Escalabilidade de Threads](#29-seção-1--escalabilidade-de-threads-de-cálculo)
30. [Seção 2 — Escalabilidade de Núcleos RV32I](#30-seção-2--escalabilidade-de-núcleos-rv32i)
31. [Seção 3 — Impacto da Resolução](#31-seção-3--impacto-da-resolução)
32. [Seção 4 — Grade Cores × Threads](#32-seção-4--grade-cores--threads)
33. [Lei de Amdahl — Análise Quantitativa Completa](#33-lei-de-amdahl--análise-quantitativa-completa)
34. [Conclusões dos Benchmarks e Recomendações](#34-conclusões-dos-benchmarks-e-recomendações)
35. [Resultados Finais — Benchmark 2026-06-04 e Artigo IEEE](#35-resultados-finais--benchmark-2026-06-04-e-artigo-ieee)

---

## 1. O que é este projeto?

O **GPU-V** é um emulador de GPU escrito em C++. Ele simula uma placa gráfica que possui **64 "shader cores"** — cada um deles é um processador RISC-V RV32I completo rodando em sua própria thread do sistema operacional.

**O objetivo principal:** mover todo o trabalho gráfico (transformação 3D, projeção perspectiva, desenho de arestas) para dentro dos núcleos simulados, exatamente como uma GPU real executa shaders em seus cores.

A CPU do computador real (o seu processador) apenas:
1. Inicializa as tabelas de sin/cos e as posições dos vértices do cubo na memória da GPU (uma única vez)
2. Atualiza o ângulo de rotação uma vez por frame (uma única escrita na memória)
3. Dispara os shaders e aguarda o resultado

Todo o resto — calcular senos e cossenos via tabela, multiplicações em ponto fixo, divisões para perspectiva, o algoritmo de Bresenham linha a linha — roda dentro dos 64 núcleos RV32I emulados, em paralelo.

### Por que isso é interessante?

Num projeto típico de gráficos você chamaria `sin()`, `cos()` e operações de ponto flutuante da sua linguagem, usando os recursos da CPU. Aqui, essas operações são feitas dentro de um processador **emulado dentro do seu programa** — um processador que executa um conjunto de instruções diferente (RISC-V), e que pode rodar em paralelo com outros 63 processadores iguais. Isso ilustra exatamente como GPUs modernas funcionam.

---

## 2. Estrutura de arquivos

```
GPU_RiscV/
├── config.h          ← constantes globais (resolução, nº cores, nº threads)
├── VRAM.h            ← classe que representa a memória da GPU
├── vram_layout.h     ← mapa de endereços dentro da VRAM
├── RV32ICore.h       ← declaração do núcleo RISC-V (registradores, PC)
├── RV32ICore.cpp     ← implementação do ciclo fetch-decode-execute
├── RV32Asm.h         ← funções que montam instruções RISC-V em 32 bits
├── geom_shader.h/.cpp← geometry shader: inicializa VRAM e monta o programa
├── frag_shader.h/.cpp← fragment shader: monta o programa de coloração
├── gpu.h/.cpp        ← GPUManager: orquestra os 64 núcleos
├── janela.h/.cpp     ← janela SDL2 para exibir o framebuffer
├── main.cpp          ← ponto de entrada e loop de animação
└── Makefile          ← regras de compilação
```

**Dependência entre arquivos:**
```
config.h  ←── todos
vram_layout.h ←── geom_shader, gpu, main
VRAM.h ←── RV32ICore, gpu
RV32Asm.h ←── geom_shader, frag_shader
RV32ICore ←── gpu
geom_shader, frag_shader ←── main
gpu ←── main
janela ←── main
```

---

## 3. config.h — constantes globais

```cpp
constexpr int FB_W  = 128;   // largura em pixels
constexpr int FB_H  = 128;   // altura em pixels
constexpr int SCALE = 5;     // fator de escala da janela SDL
constexpr int NUM_CORES   = 64;
constexpr int NUM_THREADS = 0; // 0 = detectar automaticamente
```

### `constexpr`

`constexpr` significa que o valor é conhecido **em tempo de compilação**. Isso permite que o compilador substitua diretamente o valor numérico em todos os lugares onde a constante é usada, sem custo de runtime.

### FB_W e FB_H

O framebuffer tem 128×128 = **16.384 pixels**. Esse número aparece por toda a codebase como `N`. A resolução é pequena intencionalmente: o objetivo é demonstrar a arquitetura, não produzir imagens em alta resolução.

### SCALE = 5

A janela na tela terá 128×5 = **640×640 pixels**. O SDL2 estica a textura de 128×128 para 640×640 automaticamente. O usuário vê uma imagem maior sem que o framebuffer interno cresça.

### NUM_CORES = 64

O projeto cria 64 objetos `RV32ICore`, cada um representando um núcleo de processamento. No fragment shader, cada núcleo processa 16384 ÷ 64 = **256 pixels** em paralelo.

### NUM_THREADS = 0

Quando 0, o código usa `std::thread::hardware_concurrency()` para descobrir quantas threads o processador real suporta e executa os 64 núcleos em batches desse tamanho. Se o seu PC tem 8 threads, os 64 núcleos rodam em 8 batches de 8.

---

## 4. O que é RISC-V?

**RISC-V** (lê-se "risco cinco") é uma **arquitetura de conjunto de instruções (ISA)** aberta e gratuita. ISA é a "linguagem de máquina" que um processador entende — define quais operações existem e como são codificadas em bits.

### Por que RISC?

**RISC** = *Reduced Instruction Set Computer*. Poucas instruções, cada uma fazendo exatamente uma coisa. Isso simplifica a implementação do hardware e facilita a emulação em software (como neste projeto).

**Contraste com CISC** (como x86): instruções complexas que podem fazer memória-para-memória numa única instrução, muito difíceis de emular corretamente.

### RV32I — a base

**RV32I** = RISC-V, 32 bits, subconjunto de inteiros base.

- **32 registradores** de 32 bits cada (`x0` a `x31`):
  - `x0` é sempre zero — hardwired (qualquer escrita em x0 é descartada)
  - Os demais são de uso geral; a ABI (convenção de chamada) dá apelidos: `a0`=x10, `t0`=x5, `s0`=x8, etc.
- **Instruções de 32 bits** com campos em posições fixas
- Operações: aritméticas, lógicas, desvios condicionais, load/store

### Extensão M

Adiciona multiplicação e divisão inteira:

| Instrução | Operação |
|-----------|----------|
| `MUL rd, rs1, rs2` | rd = (rs1 × rs2) bits [31:0] |
| `MULH rd, rs1, rs2` | rd = (rs1 × rs2) bits [63:32] — parte alta |
| `DIV rd, rs1, rs2` | rd = rs1 ÷ rs2 (com sinal) |
| `DIVU rd, rs1, rs2` | rd = rs1 ÷ rs2 (sem sinal) |
| `REM rd, rs1, rs2` | rd = rs1 % rs2 (com sinal) |

Neste projeto usamos RV32I + M. Não usamos extensão F (ponto flutuante) — toda a matemática é feita em ponto fixo inteiro.

### Formato das instruções — bits de 0 a 31

O RISC-V organiza os 32 bits de cada instrução em **campos fixos**. Isso é crucial: não importa qual instrução, os campos `rd`, `rs1`, `rs2` estão sempre nos mesmos bits. Isso simplifica enormemente o hardware de decodificação (e a nossa emulação em C++).

**Tipo-R** (registrador–registrador): ADD, SUB, MUL, XOR, etc.
```
 31      25 24    20 19    15 14  12 11     7 6      0
┌─────────┬────────┬────────┬──────┬────────┬────────┐
│ funct7  │  rs2   │  rs1   │funct3│   rd   │ opcode │
│  7 bits │ 5 bits │ 5 bits │3 bits│ 5 bits │ 7 bits │
└─────────┴────────┴────────┴──────┴────────┴────────┘
```

**Tipo-I** (imediato): ADDI, LW, JALR, etc.
```
 31              20 19    15 14  12 11     7 6      0
┌─────────────────┬────────┬──────┬────────┬────────┐
│    imm[11:0]    │  rs1   │funct3│   rd   │ opcode │
│    12 bits      │ 5 bits │3 bits│ 5 bits │ 7 bits │
└─────────────────┴────────┴──────┴────────┴────────┘
```

O **opcode** (7 bits, posição [6:0]) identifica a família de instrução. **funct3** (3 bits, posição [14:12]) e **funct7** (7 bits, posição [31:25]) distinguem instruções dentro da mesma família.

---

## 5. O que é uma GPU e como ela funciona?

Uma **GPU** é um processador massivamente paralelo. A ideia fundamental:

> Em vez de um processador poderoso fazendo tudo sequencialmente, use **centenas de processadores simples** fazendo coisas similares ao mesmo tempo.

### Shaders

**Shaders** são pequenos programas que rodam dentro dos cores da GPU. No pipeline moderno há dois tipos principais usados neste projeto:

| Tipo | O que recebe | O que produz | Executado |
|------|-------------|--------------|-----------|
| **Vertex/Geometry Shader** | Coordenadas 3D | Posição 2D na tela | Uma vez por vértice |
| **Fragment Shader** | Posição do pixel | Cor final do pixel | Uma vez por pixel |

No GPU-V:

| Nosso nome | Equivalente moderno | O que faz |
|------------|---------------------|-----------|
| **Geometry Shader** (núcleo 0) | Vertex Shader | Rotaciona vértices, projeta em perspectiva, rasteriza arestas via Bresenham |
| **Fragment Shader** (núcleos 0–63) | Fragment Shader | Lê a edge mask, pinta pixel azul ou preto |

### MIMD vs SIMD

GPUs modernas usam principalmente **SIMD** (Single Instruction, Multiple Data): todas as "lanes" executam a mesma instrução simultaneamente mas sobre dados diferentes. É extremamente eficiente quando todos os pixels fazem a mesma operação.

Nossa GPU usa **MIMD** (Multiple Instruction, Multiple Data): cada núcleo pode executar instruções totalmente diferentes. O geometry shader (núcleo 0) faz trabalho completamente diferente dos fragment shaders (núcleos 1–63). Isso é mais flexível mas menos eficiente energeticamente.

---

## 6. Visão geral do pipeline

O pipeline de renderização por frame:

```
╔═══════════════════════════════════════════════════╗
║         INICIALIZAÇÃO (executada 1× só)           ║
║  CPU calcula LUT sin/cos (256 valores Q8.8)       ║
║  CPU grava posições dos 8 vértices do cubo (Q8.8) ║
║  Ambos escritos diretamente na VRAM               ║
╚═══════════════════════════════════════════════════╝
                        ↓ por frame
╔═══════════════════════════════════════════════════╗
║  CPU escreve angle_idx (0–255) na VRAM[2N]        ║
╚═══════════════════════════════════════════════════╝
                        ↓
╔═══════════════════════════════════════════════════╗
║  GEOMETRY SHADER — Núcleo 0 (sequencial)          ║
║  1. Lê angle_idx da VRAM                          ║
║  2. Busca sin/cos nas LUTs                        ║
║  3. Para cada um dos 8 vértices:                  ║
║     a. Aplica rotação Y  (sin_y, cos_y)           ║
║     b. Aplica rotação X  (sin_x = 0.6×sin_y)     ║
║     c. Projeção perspectiva → (sx, sy) em pixels  ║
║     d. Grava (sx, sy) na VRAM                     ║
║  4. Limpa toda a edge mask (N words = 0)          ║
║  5. Para cada uma das 12 arestas do cubo:         ║
║     Algoritmo de Bresenham: marca pixels na mask  ║
╚═══════════════════════════════════════════════════╝
                        ↓
╔═══════════════════════════════════════════════════╗
║  FRAGMENT SHADER — 64 núcleos em paralelo         ║
║  Núcleo k processa pixels [k×256 .. k×256+255]    ║
║    SE mask[pixel] == 1 → color[pixel] = AZUL      ║
║    SENÃO              → color[pixel] = PRETO       ║
╚═══════════════════════════════════════════════════╝
                        ↓
╔═══════════════════════════════════════════════════╗
║  GPUManager: swap de buffers                      ║
║  front_buffer ← color buffer do back_buffer       ║
║  SDL2: exibe front_buffer na janela               ║
╚═══════════════════════════════════════════════════╝
```

---

## 7. VRAM.h — a memória da GPU

```cpp
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
};
```

### O que é a VRAM?

Em GPUs reais, a VRAM (Video RAM) é uma memória dedicada fisicamente separada da RAM do computador, mais rápida e com barramento mais largo para atender muitos cores simultaneamente.

Neste emulador, a VRAM é simplesmente um `std::vector<uint32_t>` — um vetor de inteiros de 32 bits na RAM normal do computador. Toda leitura e escrita de VRAM pelos núcleos RV32I passa por `read_pixel` e `write_pixel`.

### Por que `uint32_t`?

Cada posição armazena 32 bits. Isso serve tanto para pixels (RGBA, 8 bits por canal) quanto para inteiros em ponto fixo Q8.8 (valores de sin/cos, coordenadas de vértices) quanto para valores simples como `angle_idx`. A interpretação do conteúdo depende de qual região da VRAM está sendo acessada.

### `explicit VRAM(size_t size)`

A palavra-chave `explicit` impede que o compilador use este construtor para conversões implícitas (como `VRAM v = 16384;`). Boa prática para construtores com um único parâmetro.

### Verificação de bounds

```cpp
if (address < memory.size())
```

Protege contra acessos fora dos limites. Se um shader tiver um bug e calcular um endereço inválido, o programa não trava — simplesmente ignora a escrita ou retorna 0 na leitura. Importante: como `address` é `uint32_t` (sem sinal), não é necessário checar `address >= 0`.

### Segurança com múltiplas threads

O comentário no `.h` menciona que "escrita lock-free é segura pois cada núcleo escreve em regiões disjuntas". Isso significa que os 64 núcleos fragment shader **nunca escrevem no mesmo endereço**: o núcleo `k` escreve apenas nos pixels `[k×256, k×256+255]`. Não há condição de corrida e não é necessário mutex.

---

## 8. vram_layout.h — mapa de endereços

Este arquivo define onde cada tipo de dado vive dentro da VRAM. É o "mapa" da memória.

```cpp
inline size_t vl_color_base  (size_t /*N*/) { return 0; }
inline size_t vl_mask_base   (size_t N)     { return N; }
inline size_t vl_angle_addr  (size_t N)     { return 2*N + 0; }
inline size_t vl_sin_base    (size_t N)     { return 2*N + 1; }
inline size_t vl_cos_base    (size_t N)     { return 2*N + 257; }
inline size_t vl_verts_base  (size_t N)     { return 2*N + 513; }
inline size_t vl_screen_base (size_t N)     { return 2*N + 537; }
inline size_t vl_total_size  (size_t N)     { return 2*N + 553; }

constexpr int FP_SCALE = 256;
```

### Layout visual completo (N = 16384)

```
Índice [word]   Tamanho     Conteúdo
─────────────────────────────────────────────────────────────
[0]             1 word      color_buffer[0]  ← pixel (0,0)
[1]             1 word      color_buffer[1]  ← pixel (1,0)
...
[16383]         1 word      color_buffer[16383] ← pixel (127,127)
─────────────────────────────────────────────────────────────
[16384]         1 word      edge_mask[0]
...
[32767]         1 word      edge_mask[16383]
─────────────────────────────────────────────────────────────
[32768]         1 word      angle_idx  (0–255)
[32769]         1 word      sin_lut[0]   = sin(0°)   × 256 = 0
[32770]         1 word      sin_lut[1]   = sin(1.4°) × 256 ≈ 6
...
[33024]         1 word      sin_lut[255] = sin(358.6°) × 256 ≈ -6
[33025]         1 word      cos_lut[0]   = cos(0°) × 256 = 256
...
[33280]         1 word      cos_lut[255]
[33281]         1 word      cube_verts[0].x = -256 (vértice 0, coordenada x)
[33282]         1 word      cube_verts[0].y = -256
[33283]         1 word      cube_verts[0].z = -256
...
[33304]         1 word      cube_verts[7].z = +256
[33305]         1 word      screen_verts[0].x  (após rotação e projeção)
[33306]         1 word      screen_verts[0].y
...
[33320]         1 word      screen_verts[7].y
─────────────────────────────────────────────────────────────
Total: 33321 words = 2×16384 + 553
```

### Por que endereçamento por palavra (word), não por byte?

Em RISC-V real, `LW` recebe um endereço de byte e carrega 4 bytes. Neste emulador, simplificamos: `LW` usa o valor do registrador como **índice de palavra** diretamente — `vram.memory[addr]`. Isso significa que quando o shader escreve `LW x15, x10, 0` com `x10 = 16384`, ele lê exatamente `memory[16384]` — sem precisar multiplicar por 4.

Consequência prática: os shaders podem usar `ADDI x6, x5, 256` para avançar 256 posições no array de LUT, o que é direto e intuitivo.

### `inline` e `constexpr FP_SCALE`

As funções `vl_*` são `inline` — o compilador as insere diretamente no ponto de chamada, sem overhead de chamada de função. `FP_SCALE = 256` é a escala do ponto fixo Q8.8, compartilhada por toda a pipeline.

---

## 9. Aritmética de ponto fixo Q8.8

Processadores inteiros não entendem números com vírgula como `0.707`. Para fazer matemática fracionária **sem FPU** (Floating Point Unit), usamos **ponto fixo**.

### A ideia central

Escolhemos uma **escala — neste projeto 256 (= 2⁸)** — e representamos qualquer número real multiplicando por essa escala:

```
número real → inteiro armazenado
────────────────────────────────
 1.0         →  256
 0.5         →  128
-1.0         → -256
 0.707       →  181   (≈ sin 45°)
 3.5         →  896   (= 3.5 × 256)
```

O formato se chama **Q8.8** porque os 32 bits do inteiro são interpretados como: 8 bits para a parte inteira + 8 bits para a parte fracionária + 16 bits de sinal/extensão.

### Adição e subtração

São triviais — adicionar dois Q8.8 dá Q8.8:
```
(A_real + B_real) × 256 = A_fp + B_fp
```

### Multiplicação Q8.8 × Q8.8

Se `A` e `B` são valores Q8.8:
```
A_real = A_fp / 256
B_real = B_fp / 256
A_real × B_real = (A_fp × B_fp) / 65536
```

Para recuperar o resultado em Q8.8 (e não Q16.16):
```
resultado_Q8.8 = (A_fp × B_fp) / 256 = (A_fp × B_fp) >> 8
```

Em assembly RISC-V: `MUL rd, rs1, rs2` seguido de `SRAI rd, rd, 8`.

**Exemplo numérico concreto:**
```
cos(0°) em Q8.8 = 256
sin(45°) em Q8.8 = 181

produto bruto = 256 × 181 = 46336
resultado Q8.8 = 46336 >> 8 = 181  ✓ (representa 0.707, que é sin(45°) × cos(0°))
```

### Por que escala 256 e não 65536?

```
Com Q8.8 (escala 256):
  Maior produto possível: 256 × 256 = 65536
  65536 < 2^31 = 2147483648 → cabe em int32_t ✓

Com Q16.16 (escala 65536):
  Maior produto: 65536 × 65536 = 2^32 = 4294967296
  4294967296 > 2^31 → OVERFLOW em int32_t ✗
  Precisaria de MULH (parte alta da multiplicação 64 bits) ✗
```

A escolha de escala 256 mantém todos os produtos dentro de 32 bits, eliminando a necessidade de aritmética de 64 bits nos shaders.

### Divisão Q8.8

Para a projeção perspectiva precisamos calcular `3.5 / (3.5 + vz)`. Em Q8.8:

```
numerador   = 3.5 × 256 × 256 = 229376   (precisa de 256² porque dividiremos por um Q8.8)
denominador = (3.5 + vz) em Q8.8 = 896 + vz_fp
resultado_Q8.8 = 229376 / (896 + vz_fp)   ← usa instrução DIV
```

O fator 256² no numerador compensa o fato de que o divisor já está em Q8.8: `(A × 256) / (B × 256) = A/B`, mas queremos `(A/B) × 256`, então multiplicamos o numerador por 256 extra.

---

## 10. Matemática 3D: rotação e projeção

### Representação do cubo

O cubo unitário tem vértices em todas as combinações de `(±1, ±1, ±1)`. Em Q8.8 (escala 256):

```cpp
static const int CUBE_VERTS_FP[8][3] = {
    {-256,-256,-256},  // v0: canto frontal inferior esquerdo
    { 256,-256,-256},  // v1: canto frontal inferior direito
    { 256, 256,-256},  // v2: canto frontal superior direito
    {-256, 256,-256},  // v3: canto frontal superior esquerdo
    {-256,-256, 256},  // v4: canto traseiro inferior esquerdo
    { 256,-256, 256},  // v5: canto traseiro inferior direito
    { 256, 256, 256},  // v6: canto traseiro superior direito
    {-256, 256, 256}   // v7: canto traseiro superior esquerdo
};
```

E as 12 arestas (cada face quadrada tem 4, são 6 faces, mas cada aresta é compartilhada):

```cpp
static const int CUBE_EDGES[12][2] = {
    {0,1},{1,2},{2,3},{3,0},  // face frontal (z=-1)
    {4,5},{5,6},{6,7},{7,4},  // face traseira (z=+1)
    {0,4},{1,5},{2,6},{3,7}   // arestas laterais conectando frente e trás
};
```

### Rotação em 3D

Para animar o cubo girando, aplicamos duas rotações em sequência:

**Rotação em torno do eixo Y** com ângulo `ay` (o ângulo principal de animação):
```
vx' =  vx × cos(ay) + vz × sin(ay)
vy' =  vy                             ← eixo Y não muda
vz' = -vx × sin(ay) + vz × cos(ay)
```

**Rotação em torno do eixo X** com ângulo `ax = 0.6 × ay` (inclinação diagonal):
```
vx'' =  vx'                           ← eixo X não muda
vy'' =  vy' × cos(ax) - vz' × sin(ax)
vz'' =  vy' × sin(ax) + vz' × cos(ax)
```

**Por que ax = 0.6 × ay?**

Se girássemos apenas em Y, o cubo pareceria uma linha quando visto de lado. A rotação X adicional inclina o cubo, revelando a face superior. A proporção 0.6 foi escolhida empiricamente para dar uma visão esteticamente agradável.

**Em ponto fixo Q8.8** (onde `cos_y`, `sin_y`, etc. são valores Q8.8):
```
vx' = (vx × cos_y + vz × sin_y) >> 8
vz_tmp = (-vx × sin_y + vz × cos_y) >> 8   ← salvo temporariamente
vy' = (vy × cos_x - vz_tmp × sin_x) >> 8
vz' = (vy × sin_x + vz_tmp × cos_x) >> 8
```

Cada par `MUL + SRAI(8)` é uma multiplicação Q8.8.

### Tabela LUT para sin/cos

Em vez de calcular `sin()` e `cos()` dentro do shader (o que requereria instruções de ponto flutuante ou uma série de Taylor em inteiros), pré-calculamos uma tabela de 256 valores.

O ângulo de animação é quantizado em 256 passos (cada passo = 360°/256 ≈ 1.4°):

```cpp
// Em init_geometry_vram():
for (int i = 0; i < 256; i++) {
    double ang = 2.0 * M_PI * i / 256.0;
    vram.memory[sin_base + i] = (uint32_t)(int32_t)std::round(std::sin(ang) * 256.0);
    vram.memory[cos_base + i] = (uint32_t)(int32_t)std::round(std::cos(ang) * 256.0);
}
```

O cast `(uint32_t)(int32_t)` é necessário porque valores negativos (como sin(180°) = -0 ou sin(270°) ≈ -256) precisam ser armazenados como inteiros com sinal e depois reinterpretados como uint32_t para armazenamento no vector.

O geometry shader carrega o ângulo da VRAM, busca as LUTs, e obtém sin/cos em Q8.8 com um simples `LW`.

Para o ângulo X: `ax_idx = (angle_idx × 154) >> 8`. O valor 154 vem de `0.6 × 256 ≈ 154`. Multiplicar por 154 e deslocar 8 bits é equivalente a multiplicar por 0.6 em Q8.8.

### Projeção perspectiva

Projeção perspectiva simula a percepção de profundidade: objetos mais distantes aparecem menores. A câmera está em `z = -3.5` (negativo = na frente), olhando para o cubo em `z ≈ 0`.

A fórmula contínua:
```
d = 3.5 / (3.5 + vz)
sx = (vx × d × 0.45 + 0.5) × 128
sy = (-vy × d × 0.45 + 0.5) × 128
```

- `3.5 + vz`: distância do ponto à câmera
- `d`: fator de perspectiva — perto de 1 quando z≈0, menor quando z aumenta
- `0.45`: escala horizontal/vertical (ajuste de campo de visão)
- `+0.5` e `× 128`: centraliza o resultado em 0..127
- O sinal negativo em `vy` é porque y cresce para baixo em coordenadas de tela (SDL), mas para cima em coordenadas 3D

**Em ponto fixo Q8.8:**
```
numerador = 3.5 × 256 × 256 = 229376 = 56 × 4096 = 56 << 12
denominador = 896 + vz_fp   (onde 896 = 3.5 × 256)

d_fp = 229376 / denominador   ← instrução DIV

sx = ((vx_fp × d_fp) >> 8) × 58 >> 8 + 64
```

Onde `58 ≈ 0.45 × 128` e `64 = 128/2` (centro da tela).

---

## 11. RV32ICore.h — estrutura do núcleo

O header declara a classe que representa um núcleo RISC-V completo:

```cpp
class RV32ICore {
private:
    uint32_t pc;                               // Program Counter
    uint32_t registers[32];                    // registradores x0..x31
    uint32_t mhartid;                          // ID único deste núcleo
    VRAM& vram_ref;                            // referência à VRAM compartilhada
    std::vector<uint32_t> instruction_memory;  // programa em execução

    int32_t imm_I(uint32_t inst);  // extrai imediato tipo-I (12 bits)
    int32_t imm_S(uint32_t inst);  // extrai imediato tipo-S (store)
    int32_t imm_B(uint32_t inst);  // extrai imediato tipo-B (branch)
    int32_t imm_U(uint32_t inst);  // extrai imediato tipo-U (upper)
    int32_t imm_J(uint32_t inst);  // extrai imediato tipo-J (jump)
};
```

### Por que `VRAM& vram_ref` é referência e não cópia?

Todos os 64 núcleos compartilham a **mesma** VRAM. Uma referência (`&`) garante que todos apontam para o mesmo objeto — sem custo de cópia. Se fosse `VRAM vram_ref` (sem `&`), cada núcleo teria uma VRAM independente e não poderiam trocar dados (os shaders não funcionariam).

### Funções de extração de imediatos

O RISC-V tem 5 formatos de imediato com bits em posições diferentes. O motivo da "bagunça" é que o hardware de decodificação fica mais simples quando campos como `rd`, `rs1` e `rs2` ficam sempre nos mesmos bits. Os bits do imediato preenchem os "buracos" restantes.

**imm_I — Tipo-I (ADDI, LW, etc.)**
```cpp
int32_t imm_I(uint32_t inst) { return (int32_t)inst >> 20; }
```
Os bits [31:20] são o imediato de 12 bits. Cast para `int32_t` + shift aritmético (`>>`) faz **extensão de sinal** automaticamente: se o bit 31 é 1, os bits [31:12] do resultado ficam todos 1 (representando negativo).

Exemplo: `ADDI x5, x0, -1` → `inst = 0xFFF00293`, `(int32_t)inst >> 20 = -1` ✓

**imm_S — Tipo-S (SW)**
```cpp
int32_t imm_S(uint32_t inst) {
    uint32_t imm = ((inst >> 7) & 0x1F) | (((inst >> 25) & 0x7F) << 5);
    return (imm & 0x800) ? (int32_t)(imm | 0xFFFFF000) : (int32_t)imm;
}
```
O imediato de 12 bits está dividido: bits [4:0] nos bits [11:7] da instrução, bits [11:5] nos bits [31:25]. A linha monta manualmente o valor de 12 bits e faz extensão de sinal checando o bit 11 (`0x800`).

**imm_B — Tipo-B (branches)**
```cpp
int32_t imm_B(uint32_t inst) {
    uint32_t imm = ((inst >> 8)  & 0xF)  << 1  |
                   ((inst >> 25) & 0x3F)  << 5  |
                   ((inst >> 7)  & 0x1)   << 11 |
                   ((inst >> 31) & 0x1)   << 12;
    return (imm & 0x1000) ? (int32_t)(imm | 0xFFFFE000) : (int32_t)imm;
}
```
Offset de salto em bytes, sempre múltiplo de 2 (bit 0 implicitamente zero). Os 13 bits úteis estão em 4 grupos espalhados, reconstruídos manualmente. Range: −4096 a +4094 bytes (≈ ±1023 instruções).

**imm_U — Tipo-U (LUI, AUIPC)**
```cpp
int32_t imm_U(uint32_t inst) { return (int32_t)(inst & 0xFFFFF000); }
```
Mantém os 20 bits altos [31:12], zerando os 12 baixos. Já inclui extensão de sinal porque preserva o bit 31.

**imm_J — Tipo-J (JAL)**
```cpp
int32_t imm_J(uint32_t inst) {
    uint32_t imm = ((inst >> 21) & 0x3FF) << 1  |
                   ((inst >> 20) & 0x1)   << 11 |
                   ((inst >> 12) & 0xFF)  << 12 |
                   ((inst >> 31) & 0x1)   << 20;
    return (imm & 0x100000) ? (int32_t)(imm | 0xFFE00000) : (int32_t)imm;
}
```
Offset de 21 bits (múltiplo de 2), bits também embaralhados para reutilizar o mesmo hardware de decode do processador real.

---

## 12. RV32ICore.cpp — ciclo fetch-decode-execute

### Construtor

```cpp
RV32ICore::RV32ICore(uint32_t id, VRAM& vram)
    : pc(0), mhartid(id), vram_ref(vram) {
    std::fill(std::begin(registers), std::end(registers), 0u);
    registers[10] = mhartid;  // a0 = mhartid (convenção ABI RISC-V)
}
```

`pc = 0`: o programa começa na primeira instrução. `registers[10] = mhartid`: pela ABI RISC-V, `a0` (x10) é o primeiro argumento. Assim o shader sabe qual núcleo ele é sem instrução extra — basta ler x10.

### step() — um ciclo completo

```cpp
bool RV32ICore::step() {
    uint32_t word_idx = pc / 4;
    if (word_idx >= instruction_memory.size()) return false;
    uint32_t inst = instruction_memory[word_idx];  // FETCH
```

`pc / 4`: converte endereço de byte para índice de palavra no vetor. RISC-V usa endereços de bytes; instruções ocupam 4 bytes cada. Se PC ultrapassou o fim do programa, retorna `false`.

```cpp
    // DECODE — campos sempre nos mesmos bits
    uint32_t opcode = inst & 0x7F;           // bits [6:0]
    uint32_t rd     = (inst >> 7)  & 0x1F;  // bits [11:7]
    uint32_t funct3 = (inst >> 12) & 0x07;  // bits [14:12]
    uint32_t rs1    = (inst >> 15) & 0x1F;  // bits [19:15]
    uint32_t rs2    = (inst >> 20) & 0x1F;  // bits [24:20]
    uint32_t funct7 = (inst >> 25) & 0x7F;  // bits [31:25]
    uint32_t next_pc = pc + 4;              // padrão: instrução seguinte
```

Todos os campos são extraídos de uma vez. `next_pc = pc + 4` é o valor padrão; branches e jumps o sobrescrevem.

#### opcode 0x33 — instruções Tipo-R

```cpp
case 0x33: {
    uint32_t a = registers[rs1];
    uint32_t b = registers[rs2];
    switch ((funct7 << 3) | funct3) {
        case 0x000: registers[rd] = a + b;   break; // ADD
        case 0x100: registers[rd] = a - b;   break; // SUB
        ...
        case 0x008: registers[rd] = (uint32_t)((int32_t)a * (int32_t)b); break; // MUL
        case 0x00C: registers[rd] = (b==0) ? 0xFFFFFFFFu
                                           : (uint32_t)((int32_t)a / (int32_t)b); break; // DIV
    }
}
```

**O truque `(funct7 << 3) | funct3`:** concatena os dois campos de controle num único inteiro de 10 bits, permitindo um `switch` único para todas as instruções tipo-R.

Chaves resultantes:
- ADD: `(0x00 << 3) | 0 = 0x000`
- SUB: `(0x20 << 3) | 0 = 0x100` (0x20 = 32, 32×8 = 256 = 0x100)
- MUL: `(0x01 << 3) | 0 = 0x008`
- DIV: `(0x01 << 3) | 4 = 0x00C`

**MUL:** cast para `int32_t` antes de multiplicar garante semântica com sinal. O cast final `(uint32_t)` trunca para 32 bits baixos.

**DIV:** protegido contra divisão por zero: RISC-V especifica que `DIV(x, 0) = 0xFFFFFFFF (-1)`.

#### opcode 0x13 — Tipo-I (ALU imediata)

```cpp
case 0x13: {
    uint32_t a    = registers[rs1];
    int32_t  imm  = imm_I(inst);
    uint32_t shamt = rs2;  // campo rs2 reaproveitado como quantidade de shift
    switch (funct3) {
        case 0x0: registers[rd] = a + (uint32_t)imm;  break; // ADDI
        case 0x3: registers[rd] = (a < (uint32_t)imm); break; // SLTIU
        case 0x5:
            if (funct7 == 0x20)
                registers[rd] = (int32_t)a >> shamt;  // SRAI aritmético
            else
                registers[rd] = a >> shamt;             // SRLI lógico
            break;
    }
}
```

**SLTIU (Set Less Than Immediate Unsigned):** compara `a` como uint32_t sem sinal com `imm`. Crucial para o clipping: `SLTIU x30, x26, 128` retorna 1 apenas se `(uint32_t)x26 < 128`. Valores negativos de x26 têm valor enorme como uint32_t → falham → clipping automático de `x < 0` e `x ≥ 128` com uma instrução só.

**SRAI vs SRLI:** ambos têm `funct3=0x5`, distinguidos por `funct7`. SRAI (`(int32_t)a >> shamt`) propaga o bit de sinal — essencial para manter o sinal de valores negativos em Q8.8 após deslocamentos.

#### opcode 0x37 — LUI

```cpp
case 0x37:
    registers[rd] = (uint32_t)imm_U(inst);  // carrega 20 bits altos
    break;
```

**LUI** (Load Upper Immediate): carrega 20 bits nos bits [31:12], zerando os 12 inferiores. Usado com ADDI para carregar constantes de 32 bits:

```assembly
LUI  x10, 56    # x10 = 56 × 4096 = 229376  (os 12 bits baixos ficam 0)
# Não precisa de ADDI porque 229376 já tem os 12 bits baixos = 0
```

**O problema do bit 11:** quando os 12 bits baixos de uma constante têm o bit 11 = 1 (valor ≥ 2048), a ADDI subsequente estende o sinal e subtrai 4096. Para compensar, incrementa-se o argumento do LUI em 1:

```cpp
if (lo >= 2048) { hi++; lo -= 4096; }
```

#### opcode 0x63 — Branches

```cpp
case 0x63: {
    bool taken = false;
    switch (funct3) {
        case 0x0: taken = ((int32_t)a == (int32_t)b); break; // BEQ
        case 0x1: taken = ((int32_t)a != (int32_t)b); break; // BNE
        case 0x5: taken = ((int32_t)a >= (int32_t)b); break; // BGE
        case 0x7: taken = (ua >= ub);                  break; // BGEU
    }
    if (taken) next_pc = pc + (uint32_t)imm_B(inst);
}
```

O offset é relativo ao **PC atual** da instrução de branch, não ao next_pc. `BEQ x0, x0, offset` é salto incondicional (x0 == x0 sempre).

#### opcode 0x03 e 0x23 — Loads e Stores

```cpp
// LW — carrega word da VRAM
case 0x03: {
    uint32_t addr = registers[rs1] + (uint32_t)imm_I(inst);
    uint32_t data = vram_ref.read_pixel(addr);
    // funct3 determina tamanho e extensão de sinal:
    case 0x2: registers[rd] = data;  break;  // LW: 32 bits completos
    case 0x0: registers[rd] = (uint32_t)(int32_t)(int8_t)(data & 0xFF); break; // LB
}

// SW — armazena na VRAM
case 0x23: {
    uint32_t addr = registers[rs1] + (uint32_t)imm_S(inst);
    if (funct3 == 0x2)
        vram_ref.write_pixel(addr, registers[rs2]);
}
```

O endereço é calculado como `base + offset` e usado como **índice de palavra** na VRAM (não byte). `LB` faz extensão de sinal do byte menor: os casts aninhados `(int32_t)(int8_t)` primeiro sign-extendem 8→32 bits, depois o cast exterior converte para uint32_t para armazenar no registrador.

#### opcode 0x73 — ECALL (fim do shader)

```cpp
case 0x73:
    return false;  // sinaliza que o núcleo terminou
```

Em RISC-V real, `ECALL` faz chamada ao sistema operacional. Aqui simplesmente encerra a execução do núcleo.

#### Invariante x0 = 0

```cpp
    registers[0] = 0;  // x0 é hardwired zero — forçado após cada instrução
    pc = next_pc;
    return true;
```

Garante o invariante mesmo que alguma instrução tente escrever em x0.

---

## 13. RV32Asm.h — o mini-montador

Funções C++ que constroem palavras de instrução de 32 bits, evitando cálculo manual de opcodes.

### Tipo-R — construção bit a bit

```cpp
inline uint32_t ADD(int rd, int rs1, int rs2) {
    return (rs2 << 20) | (rs1 << 15) | (0x0 << 12) | (rd << 7) | 0x33;
}
```

Para `ADD x28, x15, x12` (rd=28, rs1=15, rs2=12):
```
rs2=12  → 12 << 20 = 0x00C00000
rs1=15  → 15 << 15 = 0x0001E000
funct3  →            0x00000000
rd=28   → 28 << 7  = 0x00000E00
opcode  →            0x00000033
─────────────────────────────────
OR:       0x00C1EE33
```

SUB difere apenas no funct7:
```cpp
inline uint32_t SUB(int rd, int rs1, int rs2) {
    return (0x20 << 25) | (rs2 << 20) | (rs1 << 15) | (rd << 7) | 0x33;
}
```
`0x20 << 25` posiciona funct7=0x20 nos bits [31:25].

### Tipo-I — máscara do imediato

```cpp
inline uint32_t ADDI(int rd, int rs1, int imm) {
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (rd << 7) | 0x13;
}
```

`imm & 0xFFF` mascara para 12 bits, descartando bits altos de `imm` negativos em C++ (que teriam bits [31:12] = 1 por extensão de sinal).

### SW — imediato dividido

```cpp
inline uint32_t SW(int rs2, int rs1, int imm) {
    return (((imm >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) |
           (0x2 << 12) | ((imm & 0x1F) << 7) | 0x23;
}
```

O imediato de 12 bits é dividido: bits [11:5] vão para os bits [31:25] da instrução, bits [4:0] vão para os bits [11:7]. Isso mantém rs1 e rs2 sempre nas posições padrão.

### Branches — encoding especial

```cpp
inline uint32_t _branch(int rs1, int rs2, int f3, int o) {
    return (((o>>12)&1)<<31) | (((o>>5)&0x3F)<<25) |
           (rs2<<20) | (rs1<<15) | (f3<<12) |
           (((o>>1)&0xF)<<8) | (((o>>11)&1)<<7) | 0x63;
}
```

O offset `o` em bytes é desmontado em 4 grupos:
```
o[12]    → bit 31 da instrução
o[10:5]  → bits [30:25]
o[4:1]   → bits [11:8]
o[11]    → bit 7
o[0]     → implicitamente 0, não armazenado
```

Exemplo: `BNE x24, x10, -180` → o=-180 codificado nesses campos, decodificado por `imm_B()` no núcleo para recuperar -180.

### SRAI — funct7 no shift aritmético

```cpp
inline uint32_t SRAI(int rd, int rs1, int sh) {
    return (0x20 << 25) | ((sh & 0x1F) << 20) | (rs1 << 15) | (0x5 << 12) | (rd << 7) | 0x13;
}
```

SRLI e SRAI têm mesmo opcode (0x13) e funct3 (0x5). O distinguidor é funct7: `0x00` para SRLI, `0x20` para SRAI.

---

## 14. geom_shader.cpp — shader de geometria

### init_geometry_vram — inicialização pela CPU

```cpp
void init_geometry_vram(VRAM& vram, size_t N) {
    for (int i = 0; i < 256; i++) {
        double ang = 2.0 * M_PI * i / 256.0;
        vram.memory[sin_base + i] = (uint32_t)(int32_t)std::round(std::sin(ang) * 256.0);
        vram.memory[cos_base + i] = (uint32_t)(int32_t)std::round(std::cos(ang) * 256.0);
    }
```

**Por que `(uint32_t)(int32_t)`?** `std::sin() * 256.0` retorna double com valores entre -256.0 e +256.0. O cast `(int32_t)` converte para inteiro com sinal (necessário para negativos como sin(270°) ≈ -256). O cast `(uint32_t)` reinterpreta os bits para armazenamento no vector de uint32_t. Quando o shader carrega com LW e faz MUL, os valores negativos são tratados corretamente como int32_t pelas operações aritméticas.

```cpp
    size_t vb = vl_verts_base(N);  // = 2N+513
    for (int v = 0; v < 8; v++) {
        vram.memory[vb + v*3 + 0] = (uint32_t)(int32_t)CUBE_VERTS_FP[v][0];
        vram.memory[vb + v*3 + 1] = (uint32_t)(int32_t)CUBE_VERTS_FP[v][1];
        vram.memory[vb + v*3 + 2] = (uint32_t)(int32_t)CUBE_VERTS_FP[v][2];
    }
}
```

Cada vértice usa 3 words consecutivas (x, y, z). O vértice `v` começa em `vb + v*3`.

### build_geometry_shader — prólogo

```cpp
// x4 = N = 16384
uint32_t hi = (N >> 12) & 0xFFFFF;  // 16384 >> 12 = 4
int32_t  lo = (int32_t)(N & 0xFFF); // 16384 & 0xFFF = 0
p.push_back(LUI (4, (int)hi));       // x4 = 4 << 12 = 16384
p.push_back(ADDI(4, 4, lo));         // x4 = 16384 + 0

p.push_back(ADD (5, 4, 4));    // x5 = 2N (sin_base - 1 = angle_addr)
p.push_back(ADDI(5, 5, 1));    // x5 = 2N+1 = sin_base
p.push_back(ADDI(6, 5, 256));  // x6 = 2N+257 = cos_base
p.push_back(ADDI(7, 6, 256));  // x7 = 2N+513 = verts_base
p.push_back(ADDI(8, 7, 24));   // x8 = 2N+537 = screen_base
```

Esses registradores funcionam como ponteiros permanentes para as regiões da VRAM. Calculados uma vez no prólogo, usados em todo o shader.

```cpp
p.push_back(LW(10, 5, -1));    // x10 = VRAM[2N+1-1] = VRAM[2N] = angle_idx
```

Usar `sin_base - 1 = 2N` evita guardar o endereço de angle_idx separadamente.

```cpp
// ax_idx ≈ angle_idx × 0.6 via multiplicação Q8.8
p.push_back(ADDI(28, 0, 154));     // 154/256 ≈ 0.602
p.push_back(MUL (28, 10, 28));     // angle_idx × 154
p.push_back(SRAI(28, 28, 8));      // >> 8 = ÷256 → ax_idx
```

### Loop de vértices — as 46 instruções

```cpp
p.push_back(ADDI(24, 0, 0));   // x24 = v = 0

// Instruções 1-6: carrega coordenadas do vértice v
p.push_back(ADDI(10,  0, 3));  // const 3
p.push_back(MUL (10, 24, 10)); // v × 3 (endereço no array de verts)
p.push_back(ADD (10, 10,  7)); // verts_base + v×3
p.push_back(LW  (15, 10,  0)); // vx = VRAM[verts_base + v×3]
p.push_back(LW  (16, 10,  1)); // vy = VRAM[verts_base + v×3 + 1]
p.push_back(LW  (17, 10,  2)); // vz = VRAM[verts_base + v×3 + 2]
```

```cpp
// Instruções 7-10: rotação Y — vx' = (vx×cos_y + vz×sin_y) >> 8
p.push_back(MUL (28, 15, 12));  // x28 = vx × cos_y
p.push_back(MUL (29, 17, 11));  // x29 = vz × sin_y
p.push_back(ADD (28, 28, 29));  // x28 = soma
p.push_back(SRAI(28, 28,  8));  // x28 = vx' (Q8.8 mantido)

// Instruções 11-14: vz_tmp = (vz×cos_y − vx×sin_y) >> 8
p.push_back(MUL (29, 15, 11));  // x29 = vx × sin_y
p.push_back(MUL (30, 17, 12));  // x30 = vz × cos_y
p.push_back(SUB (30, 30, 29));  // x30 = vz×cos_y − vx×sin_y
p.push_back(SRAI(30, 30,  8));  // x30 = vz_tmp

// Instruções 15-18: rotação X — vy' = (vy×cos_x − vz_tmp×sin_x) >> 8
p.push_back(MUL (29, 16, 14));  // vy × cos_x
p.push_back(MUL (31, 30, 13));  // vz_tmp × sin_x
p.push_back(SUB (29, 29, 31));
p.push_back(SRAI(29, 29,  8));  // x29 = vy'

// Instruções 19-22: vz' = (vy×sin_x + vz_tmp×cos_x) >> 8
p.push_back(MUL (31, 16, 13));  // vy × sin_x
p.push_back(MUL (10, 30, 14));  // vz_tmp × cos_x
p.push_back(ADD (31, 31, 10));
p.push_back(SRAI(31, 31,  8));  // x31 = vz'
```

Cada par `MUL + SRAI(8)` é uma multiplicação Q8.8 × Q8.8 → Q8.8.

```cpp
// Instruções 23-25: projeção perspectiva
p.push_back(LUI (10, 56));       // x10 = 229376 (= 3.5 × 256²)
p.push_back(ADDI(31, 31, 896));  // x31 = 896 + vz' (896 = 3.5 × 256)
p.push_back(DIV (10, 10, 31));   // x10 = d = 229376 ÷ (896 + vz')

// Por que 229376 = 3.5 × 256²:
// queremos d_Q8.8 = (3.5_real / denom_real) × 256
// denom_real = denom_Q8.8 / 256
// logo d_Q8.8 = (3.5 × 256 × 256) / denom_Q8.8 = 229376 / denom_Q8.8
```

```cpp
// Instruções 26-31: sx = ((vx' × d) >> 8) × 58 >> 8 + 64
p.push_back(MUL (30, 28, 10));   // vx' × d  (resultado em Q16.16)
p.push_back(SRAI(30, 30,  8));   // ÷256 → Q8.8
p.push_back(ADDI(31,  0, 58));   // 58 ≈ 0.45 × 128 (fator de escala + FoV)
p.push_back(MUL (30, 30, 31));   // × 58
p.push_back(SRAI(30, 30,  8));   // ÷256 → pixels inteiros
p.push_back(ADDI(21, 30, 64));   // + 64 (centro: W/2 = 128/2)

// Instruções 32-38: sy = (-(vy' × d) >> 8) × 58 >> 8 + 64
p.push_back(MUL (30, 29, 10));
p.push_back(SRAI(30, 30,  8));
p.push_back(SUB (30,  0, 30));   // negação: y 3D cresce pra cima, tela cresce pra baixo
p.push_back(ADDI(31,  0, 58));
p.push_back(MUL (30, 30, 31));
p.push_back(SRAI(30, 30,  8));
p.push_back(ADDI(22, 30, 64));

// Instruções 39-43: grava (sx, sy) na VRAM
p.push_back(ADDI(31,  0,  2));   // const 2
p.push_back(MUL (31, 24, 31));   // v × 2
p.push_back(ADD (31, 31,  8));   // screen_base + v×2
p.push_back(SW  (21, 31,  0));   // sx
p.push_back(SW  (22, 31,  1));   // sy

// Instruções 44-46: controle do loop
p.push_back(ADDI(24, 24,  1));   // v++
p.push_back(ADDI(10,  0,  8));   // limite = 8
p.push_back(BNE (24, 10, -180)); // se v != 8 → volta 45 instruções (45×4=180)
```

### Limpeza da edge mask

```cpp
p.push_back(ADDI(10,  4,  0));   // x10 = N (início)
p.push_back(ADD (30,  4,  4));   // x30 = 2N (fim exclusivo)
// clear_loop (3 instruções):
p.push_back(SW  ( 0, 10,  0));   // VRAM[x10] = 0  (x0 é sempre zero)
p.push_back(ADDI(10, 10,  1));   // x10++
p.push_back(BNE (10, 30,  -8));  // se x10 != 2N → volta (2 instrs × 4 = 8)
```

Zera N=16384 words em um loop de 3 instruções. Simples e eficiente.

### emit_bresenham — rasterização de cada aresta

```cpp
static void emit_bresenham(std::vector<uint32_t>& p, int v0, int v1) {
    // Setup: 19 instruções
    p.push_back(LW(26, 8, v0*2));      // x26 = sx[v0]
    p.push_back(LW(27, 8, v0*2+1));    // x27 = sy[v0]
    p.push_back(LW(17, 8, v1*2));      // x17 = sx[v1]
    p.push_back(LW(18, 8, v1*2+1));    // x18 = sy[v1]
```

`v0*2` e `v1*2` são calculados em **tempo de compilação** (em C++) e embutidos como imediatos nas instruções LW. Em runtime, o núcleo não precisa calcular esses offsets.

```cpp
    // |dx| sem branch — truque do bit de sinal:
    p.push_back(SUB (19, 17, 26));     // x19 = x1 - x0
    p.push_back(SRAI(30, 19, 31));     // x30 = máscara de sinal (0x0 ou 0xFFFFFFFF)
    p.push_back(XOR (19, 19, 30));     // if neg: flip todos os bits
    p.push_back(SUB (19, 19, 30));     // if neg: +1 → complemento de 2 = abs(x19)
```

`SRAI 31` desloca o bit de sinal para todos os 32 bits: resultado é `0x00000000` (positivo) ou `0xFFFFFFFF` (negativo). `XOR` com `0xFFFFFFFF` faz NOT. `SUB` com `0xFFFFFFFF` (=-1) adiciona 1. NOT + 1 = negação (complemento de 2). Se positivo, XOR e SUB com 0 não fazem nada.

```cpp
    // step_x = (x0 < x1) ? +1 : -1
    p.push_back(SLT (21, 26, 17));     // 1 se x0 < x1
    p.push_back(SLLI(21, 21, 1));      // × 2: torna 0 ou 2
    p.push_back(ADDI(21, 21, -1));     // -1: torna -1 ou +1

    p.push_back(SUB(23, 19, 20));      // err = dx - dy
```

```cpp
    // Loop Bresenham (20 instruções, 80 bytes):
    // Verificação de bounds com clipping automático:
    p.push_back(SLTIU(30, 26, 128));   // x in [0,127]?
    p.push_back(BEQ  (30,  0, +32));   // não → skip_write
    p.push_back(SLTIU(30, 27, 128));   // y in [0,127]?
    p.push_back(BEQ  (30,  0, +24));   // não → skip_write

    // Marca pixel na edge mask
    p.push_back(SLLI (30, 27,   7));   // y × 128 (shift 7 = ×2⁷)
    p.push_back(ADD  (30, 30,  26));   // y×128 + x
    p.push_back(ADD  (30, 30,   4));   // + N (x4=N) = endereço na mask
    p.push_back(ADDI (31,  0,   1));   // x31 = 1
    p.push_back(SW   (31, 30,   0));   // VRAM[addr] = 1

    // Verifica se chegou ao destino
    p.push_back(BNE  (26, 17,  +8));   // x0 != x1 → not_done
    p.push_back(BEQ  (27, 18, +40));   // y0 == y1 → done (fim)

    // Atualiza err, x0, y0
    p.push_back(SLLI (30, 23,   1));   // e2 = 2×err
    p.push_back(ADD  (31, 30,  20));   // e2 + dy
    p.push_back(BGE  ( 0, 31, +12));   // 0 >= e2+dy → skip_x (e2 <= -dy)
    p.push_back(SUB  (23, 23,  20));   // err -= dy
    p.push_back(ADD  (26, 26,  21));   // x0 += step_x
    p.push_back(BGE  (30, 19, +12));   // e2 >= dx → skip_y
    p.push_back(ADD  (23, 23,  19));   // err += dx
    p.push_back(ADD  (27, 27,  22));   // y0 += step_y
    p.push_back(BEQ  ( 0,  0, -76));  // volta ao início do loop (-19 instrs × 4 = -76)
}
```

`BEQ x0, x0, offset` é salto incondicional: x0 == x0 sempre. O offset -76 = 19 instruções atrás = início do bloco SLTIU (topo do loop).

---

## 15. frag_shader.cpp — shader de fragmentos

```cpp
std::vector<uint32_t> build_fragment_shader(uint32_t num_cores, uint32_t N) {
    const uint32_t ppc = N / num_cores;  // 16384 / 64 = 256 pixels por núcleo
```

### Carregando ppc (256) em t0

```cpp
uint32_t ppc_hi = (ppc >> 12) & 0xFFFFF;  // 256 >> 12 = 0
int32_t  ppc_lo = (int32_t)(ppc & 0xFFF); // 256 & 0xFFF = 256
prog.push_back(LUI (5, (int)ppc_hi));  // t0 = 0 (LUI com 0 zera o registrador)
prog.push_back(ADDI(5, 5, ppc_lo));    // t0 = 0 + 256 = 256
```

### base_addr via SLL (sem MUL)

```cpp
uint32_t shift = 0;
for (uint32_t tmp = ppc; tmp > 1; tmp >>= 1) shift++;  // log2(256) = 8

prog.push_back(ADDI(30, 0, (int32_t)shift));  // x30 = 8
prog.push_back(SLL (6, 10, 30));              // t1 = mhartid << 8 = mhartid × 256
```

Como ppc=256=2⁸, multiplicar por ppc equivale a deslocar 8 bits à esquerda. `SLL` é mais rápido que `MUL`. Para o núcleo k: `base_addr = k × 256`.

### Cor azul via LUI + ADDI

```cpp
prog.push_back(LUI (8, 0x10));   // s0 = 0x00010000
prog.push_back(ADDI(8, 8, -1)); // s0 = 0x00010000 - 1 = 0x0000FFFF
```

`0x0000FFFF` no formato RRGGBBAA = R=0, G=0, B=255, A=255 → azul puro, opaco. Não é possível carregar 0x0000FFFF com um único ADDI (que tem apenas 12 bits de imediato). LUI + ADDI resolve: carrega `0x10 << 12 = 0x00010000` e subtrai 1.

### O loop de 9 instruções

```cpp
prog.push_back(ADDI(28, 0, 0));  // i = 0

prog.push_back(ADD (29, 6,  28));   // output_addr = base_addr + i
prog.push_back(ADD (30, 7,  29));   // mask_addr = N + output_addr
prog.push_back(LW  (31, 30,  0));   // t6 = edge_mask[output_addr]
prog.push_back(BEQ (31,  0, +12)); // mask == 0 → pula para SW preto
prog.push_back(SW  ( 8, 29,  0));   // escreve AZUL (s0 = 0x0000FFFF)
prog.push_back(BEQ ( 0,  0,  +8)); // pula sobre SW preto
prog.push_back(SW  ( 0, 29,  0));   // escreve PRETO (x0 = 0)
prog.push_back(ADDI(28, 28,  1));   // i++
prog.push_back(BNE (28,  5, -32)); // i != ppc → volta (-8 instrs × 4 = -32)
prog.push_back(ECALL());
```

**A estrutura if-else em assembly:**
```
BEQ mask, 0, +12   → se mask==0, salta para [endereço_atual + 12]
SW azul            → executado se mask != 0
BEQ x0, x0, +8    → salta sobre SW preto
SW preto           → executado se mask == 0
```

`+12`: o BEQ avança 12 bytes à frente de si mesmo, pulando os 2 SWs (4+4) e o BEQ incondicional (4) = 12 bytes. O destino é exatamente o `SW preto`. ✓

**mask_addr = N + output_addr:** como `output_addr` é índice no color buffer [0, N-1], e a edge mask começa em N, `N + output_addr` dá o índice exato na mask para o mesmo pixel. Um ADD resolve o mapeamento.

---

## 16. gpu.h e gpu.cpp — o gerenciador de GPU

### Double buffering

```cpp
VRAM back_buffer;   // 2N+553 words: color + mask + LUTs + verts
VRAM front_buffer;  // N words: apenas o color buffer
```

Os shaders escrevem no `back_buffer`. Quando terminam, `swap_buffers()` copia o color buffer para o `front_buffer`. O SDL2 exibe o `front_buffer`. Isso evita exibir um frame parcialmente renderizado (artefatos visuais).

### Construtor — por que emplace_back e não push_back

```cpp
processing_units.reserve(cores_count);
for (size_t i = 0; i < cores_count; ++i)
    processing_units.emplace_back((uint32_t)i, back_buffer);
```

`RV32ICore` guarda uma **referência** (`VRAM&`) ao `back_buffer`. Se o vetor `processing_units` for realocado (quando cresce), todos os objetos são movidos para nova memória — mas a referência interna continua válida porque aponta para `back_buffer`, não para o vetor.

`reserve(cores_count)` pré-aloca espaço, evitando realocações durante o loop de construção.

`emplace_back` constrói o objeto diretamente no vetor, passando os argumentos ao construtor. `push_back` criaria um objeto temporário e o moveria/copiaria — problemático para objetos com referências internas.

### dispatch_frame — thread pool com batches

```cpp
void GPUManager::dispatch_frame() {
    unsigned hw = (num_threads > 0) ? (unsigned)num_threads
                                    : std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;  // fallback se hardware_concurrency retornar 0

    for (size_t start = 0; start < num_cores; start += hw) {
        size_t end = std::min(start + (size_t)hw, num_cores);
        std::vector<std::future<void>> batch;
        batch.reserve(end - start);
        for (size_t i = start; i < end; ++i)
            batch.push_back(
                std::async(std::launch::async, run_core, &processing_units[i]));
        for (auto& f : batch) f.get();
    }
    swap_buffers();
}
```

**`std::async(launch::async, ...)`:** cria uma thread imediatamente (vs `launch::deferred` que seria lazy). Cada chamada retorna um `std::future<void>`.

**`future.get()`:** bloqueia até a thread terminar e propaga exceções. O segundo loop espera todo o batch antes de continuar para o próximo.

**Por que batches do tamanho do hardware concurrency:**
- Criar 64 threads simultâneas num processador de 8 cores reais causaria overhead de context switching.
- Batches de 8 threads mantém todas as 8 cores ocupadas sem overhead excessivo.
- O SO agenda cada thread do batch num core diferente automaticamente.

### swap_buffers

```cpp
void GPUManager::swap_buffers() {
    std::copy(
        back_buffer.memory.begin(),
        back_buffer.memory.begin() + (ptrdiff_t)total_pixels,  // apenas N words
        front_buffer.memory.begin());
    std::fill(
        back_buffer.memory.begin(),
        back_buffer.memory.begin() + (ptrdiff_t)total_pixels, 0u);
}
```

Copia apenas os primeiros N words (color buffer). As regiões de mask, LUTs e vértices não são tocadas — a mask será zerada no próximo frame pelo geometry shader; as LUTs e vértices nunca mudam.

---

## 17. janela.h e janela.cpp — a janela SDL2

### Três objetos SDL

```cpp
SDL_Window*   win;  // a janela do sistema operacional
SDL_Renderer* ren;  // contexto de renderização (acelerado por hardware)
SDL_Texture*  tex;  // textura de 128×128 pixels na GPU da placa de vídeo real
```

O SDL usa **aceleração de hardware** (OpenGL ou Vulkan por baixo). A textura vive na memória da GPU real. `SDL_UpdateTexture` transfere pixels da RAM para a GPU real. `SDL_RenderCopy` desenha a textura na janela com escalonamento (128→640).

### Conversão de formato em draw()

```cpp
void Janela::draw(const std::vector<uint32_t>& pixels) {
    for (size_t i = 0; i < sdl_buf.size(); ++i) {
        const uint32_t p = pixels[i];
        const uint8_t  r = (p >> 24) & 0xFF;  // VRAM: R nos bits [31:24]
        const uint8_t  g = (p >> 16) & 0xFF;  // VRAM: G nos bits [23:16]
        const uint8_t  b = (p >>  8) & 0xFF;  // VRAM: B nos bits [15:8]
        sdl_buf[i] = (0xFFu << 24) |           // SDL ARGB: A=255 fixo
                     ((uint32_t)r << 16) |      // SDL ARGB: R nos bits [23:16]
                     ((uint32_t)g <<  8) |      // SDL ARGB: G nos bits [15:8]
                     b;                         // SDL ARGB: B nos bits [7:0]
    }
    SDL_UpdateTexture(tex, nullptr, sdl_buf.data(), fb_w * (int)sizeof(uint32_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);  // nullptr,nullptr = toda a textura, toda a janela
    SDL_RenderPresent(ren);
}
```

**Formatos:**
- VRAM (nosso): `RRGGBBAA` — R no byte mais significativo
- SDL `ARGB8888`: `AARRGGBB` — A no byte mais significativo

Para azul `0x0000FFFF`:
```
r = (0x0000FFFF >> 24) & 0xFF = 0
g = (0x0000FFFF >> 16) & 0xFF = 0
b = (0x0000FFFF >>  8) & 0xFF = 255
sdl_buf = (255 << 24) | (0 << 16) | (0 << 8) | 255 = 0xFF0000FF ✓
```

---

## 18. main.cpp — o loop principal

```cpp
int main() {
    const size_t N = (size_t)(FB_W * FB_H);  // 128×128 = 16384

    GPUManager gpu(NUM_CORES, N, NUM_THREADS);
    init_geometry_vram(gpu.vram(), N);  // LUTs + vértices na VRAM (1×)
```

`gpu.vram()` retorna `back_buffer` por referência. `init_geometry_vram` escreve diretamente nas posições corretas usando `vram_layout.h`.

```cpp
    const auto geom_prog = build_geometry_shader((uint32_t)N, FB_W, FB_H);
    const auto frag_prog = build_fragment_shader((uint32_t)NUM_CORES, (uint32_t)N);
```

Os shaders são compilados (montados) uma única vez. São `const` — nunca mudam. O ângulo de rotação não está no programa; está na VRAM e é lido pelo shader em runtime.

```cpp
    gpu.load_program(frag_prog);  // pré-carrega frag em todos os 64 núcleos
```

O fragment shader é igual em todos os núcleos (a diferença vem do mhartid em x10). Carregar antes do loop evita 64 reloads por frame.

```cpp
    while (janela.poll_events()) {
        gpu.set_angle(angle_idx);           // CPU: 1 write na VRAM
        gpu.load_geometry(geom_prog, 0);    // reseta núcleo 0
        gpu.dispatch_geometry(0);           // executa núcleo 0 (síncrono)
        gpu.load_program(frag_prog);        // reseta todos os 64 núcleos
        gpu.dispatch_frame();               // executa 64 núcleos (paralelo) + swap
        janela.draw(gpu.get_pixels());      // converte + exibe
        angle_idx = (angle_idx + 1) & 0xFF; // 0xFF = 255, ciclo de 256 frames
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
    }
```

`& 0xFF`: equivale a `% 256` sem divisão. Mantém angle_idx em [0, 255].

`sleep_for(16ms)`: `1000ms / 16ms ≈ 62.5 fps`. Em produção, vsync seria usado.

---

## 19. Algoritmo de Bresenham — detalhado

### Estado inicial

```
x0, y0 = ponto de partida
x1, y1 = destino
dx = |x1 - x0|, dy = |y1 - y0|
step_x = sinal de (x1-x0), step_y = sinal de (y1-y0)
err = dx - dy
```

### Um passo

```
e2 = 2 × err

if e2 > -dy:          # erro suficiente para avançar em x
    err = err - dy
    x0  = x0 + step_x

if e2 < dx:           # erro suficiente para avançar em y
    err = err + dx
    y0  = y0 + step_y
```

Ambas as condições podem ser verdadeiras simultaneamente — nesse caso, avançamos em diagonal.

### Invariante matemático

O erro representa: `err = dx*(y_exato - y_atual) - dy*(x_exato - x_atual)`. Quando `err > 0`, a linha real está acima do pixel atual — avançar em y a aproxima. O algoritmo garante que o pixel marcado é sempre o mais próximo da linha reta contínua.

### Exemplo traçado: (10,10) → (13,12)

```
dx=3, dy=2, step_x=+1, step_y=+1, err=1

Iter 1: pinta (10,10)
  e2 = 2    ; e2>-dy(+2>-2)✓ → err=1-2=-1, x0=11
             ; e2<dx(+2<3)✓  → err=-1+3=2, y0=11

Iter 2: pinta (11,11)
  e2 = 4    ; e2>-dy(+4>-2)✓ → err=2-2=0, x0=12
             ; e2<dx(+4<3)✗

Iter 3: pinta (12,11)
  e2 = 0    ; e2>-dy(0>-2)✓  → err=0-2=-2, x0=13
             ; e2<dx(0<3)✓    → err=-2+3=1, y0=12

Iter 4: pinta (13,12)
  x0==x1 e y0==y1 → FIM

Pixels: (10,10), (11,11), (12,11), (13,12)
```

### Clipping com SLTIU

```assembly
SLTIU x30, x26, 128   # trata x26 como uint32_t, x30 = (x26 < 128)
```

- `x26 = 50` → `uint32_t = 50` → `50 < 128` → x30 = 1 ✓ (dentro)
- `x26 = 130` → `uint32_t = 130` → `130 < 128` → x30 = 0 ✗ (fora direita)
- `x26 = -3` como int32_t → `uint32_t = 4294967293` → `4294967293 < 128` → x30 = 0 ✗ (fora esquerda)

Uma instrução descarta `x < 0` E `x ≥ 128`.

---

## 20. Fluxo completo de um frame — trace detalhado

Com `angle_idx = 42`, traçamos o pixel `(64, 111)` de ponta a ponta.

### CPU → VRAM

```
gpu.set_angle(42)
→ back_buffer.memory[32768] = 42
```

### Geometry shader — prólogo

```
LUI x4, 4   → x4 = 16384 (N)
ADD x5,x4,x4→ x5 = 32768
ADDI x5,x5,1→ x5 = 32769 (sin_base)
ADDI x6,x5,256→ x6 = 33025 (cos_base)
ADDI x7,x6,256→ x7 = 33281 (verts_base)
ADDI x8,x7,24 → x8 = 33305 (screen_base)
LW x10,x5,-1 → x10 = VRAM[32768] = 42 (angle_idx)
ADDI x28,x0,154
MUL x28,x10,x28 → x28 = 42×154 = 6468
SRAI x28,x28,8  → x28 = 25 (ax_idx)
... carrega sin_y=165, cos_y=196, sin_x=154, cos_x=206
```

### Geometry shader — vértice v4 = (-1,-1,+1) = (-256,-256,256) em Q8.8

**Rotação Y:**
```
vx' = (-256×196 + 256×165) >> 8 = (-50176 + 42240) >> 8 = -7936 >> 8 = -31
vz_tmp = (256×196 - (-256)×165) >> 8 = (50176 + 42240) >> 8 = 92416 >> 8 = 361
```

**Rotação X:**
```
vy' = (-256×206 - 361×154) >> 8 = (-52736 - 55594) >> 8 = -108330 >> 8 = -423
vz' = (-256×154 + 361×206) >> 8 = (-39424 + 74366) >> 8 = 34942 >> 8 = 136
```

**Perspectiva:**
```
denom = 896 + 136 = 1032
d = 229376 / 1032 = 222  (Q8.8 ≈ 0.867 — vértice mais distante, aparece menor)
sx = ((−31×222)>>8)×58>>8+64 = (−6882>>8)×58>>8+64 = −26×58>>8+64 = −1508>>8+64 = −5+64 = 59
sy = (−(−423×222)>>8)×58>>8+64 = (93906>>8)×58>>8+64 = 366×58>>8+64 = 21228>>8+64 = 82+64 = 146→ fora da tela
```

sy=146 está fora de [0,127] mas isso não é problema — o Bresenham fará clipping.

### Geometry shader — limpeza e Bresenham

A edge mask é zerada. Para a aresta v4→v5 (onde v4≈(59,146) e v5≈(159,123) — v5 calculado similarmente):

Bresenham opera de (59,146) a (159,123). Pixels com y>127 são descartados pelo SLTIU. Quando o algoritmo chega a y=127 e continua para y=126, y=125... até y=111, e o x correspondente é 64:

```
Pixel (64, 111): x=64 → SLTIU 128 → 64<128 ✓; y=111 → SLTIU 128 → 111<128 ✓
addr = N + y×128 + x = 16384 + 111×128 + 64 = 16384 + 14208 + 64 = 30656
VRAM[30656] = 1
```

### Fragment shader — núcleo 55

```
mhartid = 55
base_addr = 55 × 256 = 14080
```

Para i = 192 (pixel 14080+192 = 14272):
```
output_addr = 14080 + 192 = 14272
mask_addr   = 16384 + 14272 = 30656
t6 = VRAM[30656] = 1       ← máscara setada!
SW s0(0x0000FFFF), output_addr(14272), 0
VRAM[14272] = 0x0000FFFF   ← pixel azul gravado
```

### swap_buffers e exibição

```
front_buffer.memory[14272] = 0x0000FFFF

Conversão SDL:
r=0, g=0, b=255
sdl_buf[14272] = 0xFF0000FF  (ARGB: opaco, sem vermelho, sem verde, azul)

Posição na tela:
pixel 14272 → coluna = 14272 % 128 = 64, linha = 14272 / 128 = 111
Na janela (×5): coluna = 64×5 = 320, linha = 111×5 = 555
O usuário vê um ponto azul no centro horizontal, na parte inferior da tela.
```

---

## 21. Novo modelo de threads — separação tela / cálculo

### O problema com o modelo antigo

No modelo original, `dispatch_frame()` usava `std::async(launch::async, ...)` para cada núcleo RV32I. Isso criava e destruía threads a cada frame. Threads recém-criadas precisam de tempo do SO para serem agendadas; threads que terminam precisam ser joined. Entre frames, todas dormiam. Em ferramentas como `top` ou `htop`, o processo aparecia com uso de CPU intermitente — picos durante o frame, zeros entre frames.

Além disso, não havia separação entre o código de renderização SDL (que precisa de contexto de janela) e o código de cálculo (que só usa CPU).

### O novo modelo: 8 threads com responsabilidades fixas

```
┌─────────────────────────────────────────────────────────────────────┐
│  Thread da tela  (screen_thread — lançada em main.cpp)              │
│  • Proprietária da janela SDL2, do renderer e da textura            │
│  • Chama poll_events() + janela.draw() quando há frame disponível   │
│  • Bloqueia em condition_variable com timeout de 16 ms              │
│  • Sinaliza shared.stop = true quando ESC ou janela fecha           │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  SharedFrame (mutex + condvar + pixels)
┌──────────────────────────────▼──────────────────────────────────────┐
│  Thread principal  (main thread — bloqueia em gpu.run_continuous())  │
│  Não faz cálculo diretamente — apenas lança as 7 worker threads      │
│  e aguarda o join quando shared.stop é sinalizado                    │
│                                                                       │
│  ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┐                 │
│  │  t0  │  t1  │  t2  │  t3  │  t4  │  t5  │  t6  │  ← 7 workers   │
│  │SPIN  │SPIN  │SPIN  │SPIN  │SPIN  │SPIN  │SPIN  │  SEMPRE ATIVOS  │
│  └──────┴──────┴──────┴──────┴──────┴──────┴──────┘                 │
└─────────────────────────────────────────────────────────────────────┘
```

**Total: 1 (tela) + 1 (main/coordenação) + 7 (workers) = 9 threads visíveis para o SO.** As 7 workers aparecem como 100% em `top`/`htop` desde o lançamento até o fechamento da janela — nunca dormem, nunca bloqueiam.

### Por que separar a tela do cálculo?

1. **SDL2 e thread-safety**: toda chamada SDL (`SDL_PollEvent`, `SDL_RenderPresent`) deve vir da mesma thread que criou a janela. Misturar SDL com o pool de cálculo forçaria locks caros em cada frame.

2. **Taxas de atualização independentes**: o cálculo pode produzir 500 frames/s; o monitor exibe 60 Hz. Com threads separadas e o buffer compartilhado com semântica de "último frame", a tela simplesmente pega o frame mais recente disponível sem travar o cálculo.

3. **Isolamento de falhas**: um bug no pipeline RV32I não corrompe o estado do SDL, e vice-versa.

---

## 22. SharedFrame — o buffer produtor-consumidor

```cpp
struct SharedFrame {
    std::vector<uint32_t>   pixels;  // N pixels do color buffer
    std::mutex              mtx;
    std::condition_variable cv;
    bool ready = false;  // true = há frame novo disponível
    bool stop  = false;  // true = encerrar todas as threads
};
```

### Semântica de "último frame"

O produtor (thread líder, t=0) **sobrescreve** `pixels` a cada frame, mesmo que o frame anterior ainda não tenha sido consumido pela tela. Isso é chamado de *latest-frame semantics*:

- Se a tela é mais rápida que o cálculo: ela bloqueia esperando o próximo frame (comportamento normal).
- Se o cálculo é mais rápido que a tela: frames intermediários são descartados. A tela sempre exibe o mais recente. Sem acúmulo de buffer, sem latência crescente.

Isso é diferente de uma fila FIFO (que garantiria exibir todos os frames mas adicionaria latência quando o cálculo é rápido).

### Protocolo de comunicação

**Produtor (worker t=0, após cada frame):**
```cpp
{
    std::lock_guard<std::mutex> lk(shared.mtx);
    if (shared.stop) {
        running_.store(false);  // encerra o spin loop
    } else {
        shared.pixels.assign(front_buffer...);  // copia N pixels
        shared.ready = true;
    }
}
shared.cv.notify_one();  // acorda a thread da tela
```

**Consumidor (thread da tela):**
```cpp
std::unique_lock<std::mutex> lk(shared.mtx);
if (shared.cv.wait_for(lk, 16ms, [&]{ return shared.ready || shared.stop; })) {
    if (shared.stop) break;
    janela.draw(shared.pixels);  // dentro do lock: pixels são consistentes
    shared.ready = false;
}
// timeout de 16ms: volta ao poll_events mesmo sem frame novo
```

O `wait_for` com timeout de 16 ms garante que `poll_events()` seja chamado pelo menos a cada ~16 ms, mantendo a janela responsiva ao teclado/mouse mesmo que nenhum frame seja produzido.

### Por que `assign` em vez de `swap`?

`shared.pixels.assign(front_buffer.begin(), front_buffer.begin() + N)` copia N palavras. Alternativa seria `std::swap(shared.pixels, local_copy)` — mas exigiria um buffer extra local para evitar que o front_buffer seja modificado enquanto a tela lê. A cópia é mais simples e o custo (N × 4 bytes/frame) é dominado pelo custo dos shaders RV32I.

---

## 23. SpinBarrier e 100% de utilização de CPU — explicação ultra-detalhada

### O que é 100% de CPU?

Quando o `top` ou `htop` mostram "100%" para uma thread, significa que o processador está executando instruções daquela thread continuamente — não está esperando por I/O, não está dormindo em `sleep()`, não está bloqueado em `mutex::lock()`.

Uma thread **dormindo** (em `std::this_thread::sleep_for`, `cv.wait`, `futex_wait`) é retirada da fila de execução do SO. O processador a esquece e executa outras threads. O `top` mostra 0% porque o processador não executa nenhuma instrução dela durante o sono.

Uma thread em **spin** (loop `while(condition){}`) executa a instrução de verificação de condição repetidamente, sem pausa. O SO não sabe que ela está "esperando" — do ponto de vista do hardware, ela está trabalhando. O `top` mostra 100%.

### Por que usar spin em vez de condition_variable entre fases?

Com `condition_variable`:
- Thread chama `cv.wait()` → entra no kernel Linux via syscall → kernel a retira da fila de execução → outro processo/thread ocupa o core → quando acordada, kernel a coloca de volta na fila → delay de agendamento (tipicamente 0.1–1 ms por evento).
- Para sincronização que ocorre milhares de vezes por segundo (entre fases de um frame de shader), esse overhead domina.

Com **spin**:
- Thread executa `while(gen.load() == g) CPU_RELAX();` — puro loop em userspace.
- Nenhuma syscall, nenhuma troca de contexto, nenhum delay de agendamento.
- Latência de "acordar" é medida em **nanosegundos** (quanto tempo leva para o outro core escrever no atomic e a cache local invalidar e recarregar).

O custo: consome 100% do core mesmo quando "esperando". Para um emulador de GPU onde queremos sempre máximo throughput de frames, isso é o comportamento desejado.

### Estrutura do SpinBarrier

```cpp
struct SpinBarrier {
    const int        n;       // número de threads que devem chegar
    std::atomic<int> count{0};// quantas já chegaram nesta geração
    std::atomic<int> gen{0};  // contador de geração (incrementado a cada release)

    explicit SpinBarrier(int n_) : n(n_) {}

    void arrive_and_wait();
};
```

**Por que dois atomics (`count` e `gen`)?**

Usar apenas `count` causaria o problema ABA: a última thread zera `count=0` e as threads que esperavam saem do spin. Mas uma thread muito rápida poderia chegar à **próxima** chamada de `arrive_and_wait` antes das outras saírem da **atual**, incrementando `count` para 1 novamente. As threads ainda em spin veriam `count=1` e não saberiam se é a próxima barreira ou a atual ainda em andamento.

`gen` resolve isso: cada release incrementa `gen`. As threads em spin monitoram `gen`, não `count`. Uma thread que chegou cedo à próxima barreira ficará em spin na nova geração, separada das que ainda terminam a geração anterior.

### Implementação linha a linha

```cpp
void GPUManager::SpinBarrier::arrive_and_wait() {
    // 1. Captura a geração ANTES de incrementar o contador
    int g = gen.load(std::memory_order_acquire);
```

`memory_order_acquire`: garante que todas as leituras/escritas feitas ANTES por outras threads (que liberaram esta barreira na geração anterior) sejam visíveis para esta thread após esta instrução. Isso cria um "ponto de sincronização".

```cpp
    // 2. Anuncia chegada e verifica se é a última
    if (count.fetch_add(1, std::memory_order_acq_rel) + 1 == n) {
```

`fetch_add` é atômica: lê o valor atual, adiciona 1, armazena — tudo em uma operação indivisível (em x86, se torna `LOCK XADD`). O valor **retornado** é o valor ANTES da adição. Então `+1` dá o valor DEPOIS. Se esse valor é igual a `n`, esta thread é a última a chegar.

`memory_order_acq_rel`: esta thread vê todo o trabalho das threads que chegaram antes (acquire), e seu trabalho é visível para qualquer thread que verificar `gen` depois (release).

```cpp
        // 3. Última thread: libera a barreira
        count.store(0, std::memory_order_relaxed);
        gen.fetch_add(1, std::memory_order_release);
```

Reseta `count=0` ANTES de incrementar `gen`. Importância: se incrementasse `gen` primeiro, threads esperando poderiam sair do spin, ir para a próxima barreira, e encontrar `count` ainda em valor não-zerado — corrompendo o estado.

`memory_order_release` no `gen.fetch_add`: qualquer thread que ler `gen` com `acquire` depois disso verá todo o trabalho desta thread (e, por transitividade, de todas as threads que chegaram antes).

```cpp
    } else {
        // 4. Não é a última: spin aguardando mudança de geração
        while (gen.load(std::memory_order_acquire) == g)
            CPU_RELAX();
    }
}
```

Loop de spin: verifica `gen` repetidamente. Quando a última thread incrementar `gen`, esta thread verá `gen != g` e sairá do loop. O `memory_order_acquire` garante que ela verá todos os resultados do trabalho das outras threads que aconteceram antes do release.

### CPU_RELAX — o `PAUSE` do x86

```cpp
#if defined(__x86_64__) || defined(_M_X64)
#  define CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(__arm__)
#  define CPU_RELAX() __asm__ volatile("yield" ::: "memory")
#else
#  define CPU_RELAX() ((void)0)
#endif
```

**Sem `PAUSE`**: a thread que faz spin monitora a mesma cache line do `gen` atomic em loop apertado. No protocolo MESI de coerência de cache, ela mantém a linha em estado `Shared` e bombardeia o barramento com requisições de leitura. Quando a thread detentora do core com o `gen` tenta escrever (para liberar a barreira), ela precisa esperar a invalidação ser propagada para todos os núcleos fazendo spin — o que paradoxalmente atrasa a liberação.

**Com `PAUSE`**: a instrução `PAUSE` no x86 (um único byte: `F3 90`) sinaliza ao pipeline que este loop é um spin-wait. O processador:
1. Reduz a taxa de execução especulativa (não fica prevendo branches que nunca acontecem).
2. Libera o barramento de cache de requisições desnecessárias entre iterações.
3. Consome menos energia.
4. Permite que a outra thread hiperthreading no mesmo core físico use mais recursos.

O SO ainda vê 100% de CPU — `PAUSE` não faz syscall e não dura mais que alguns ciclos. É imperceptível para o agendador.

### As 3 barreiras separadas por fase

Usamos **3 instâncias distintas** de `SpinBarrier`, uma por fase:

```cpp
SpinBarrier barrier_geom_;  // sincroniza após geometry (fim da Fase 0)
SpinBarrier barrier_frag_;  // sincroniza após fragment (fim da Fase 1)
SpinBarrier barrier_pub_;   // sincroniza após publish  (fim da Fase 2)
```

**Por que 3 e não 1 reutilizada?**

Uma única barreira é tecnicamente segura para uso sequencial *se* nenhuma thread pode completar dois ciclos da barreira antes de todas completarem um. Em nosso caso isso é garantido (o líder, t=0, é sempre o mais lento — faz todo o trabalho nas fases 0 e 2), mas usar 3 instâncias elimina a necessidade de qualquer raciocínio sobre isso. Cada barreira tem seu próprio `gen` independente. Mais seguro, mais legível.

### O corpo do worker — linha a linha

```cpp
void GPUManager::worker(int t, SharedFrame& shared) {
    while (running_.load(std::memory_order_relaxed)) {
```

O loop principal. `memory_order_relaxed`: não precisamos de garantias de ordering aqui — `running_` é escrito com `release` pela última operação antes de sair e as barreiras (que usam `acq_rel`) garantem que todas as threads verão a mudança após a barreira_pub_.

```cpp
        // ── Fase 0: geometry (apenas líder) ──────────────────────────
        if (t == 0) {
            uint32_t idx = angle_idx_.fetch_add(ROT_STEP, std::memory_order_relaxed) & 0xFF;
            back_buffer.memory[vl_angle_addr(total_pixels)] = idx;
            processing_units[0].reset();
            processing_units[0].execute();
            processing_units[0].load_program(frag_prog_);
        }
        barrier_geom_.arrive_and_wait();
```

Thread t=0 faz trabalho real; threads t=1..6 chegam imediatamente à barreira e ficam em spin, aguardando t=0 terminar o geometry shader. O `reset()` é barato (zera PC e registradores sem copiar instruction_memory). O `execute()` roda o geometry shader completo. O `load_program(frag_prog_)` troca a instruction_memory do núcleo 0 para o fragment shader (necessário porque o mesmo núcleo executa ambos).

**CPU ao longo da Fase 0:**
- Thread t=0: 100% útil (geometry shader RV32I)
- Threads t=1..6: 100% de CPU (spin no barrier_geom_) mas trabalho "inútil"

**Total no `top`:** 7 threads × 100% = 700% (em sistemas com 7+ cores físicos)

```cpp
        // ── Fase 1: fragment (TODAS as threads) ──────────────────────
        for (size_t i = (size_t)t; i < num_cores; i += (size_t)COMPUTE_THREADS) {
            processing_units[i].reset();
            processing_units[i].execute();
        }
        barrier_frag_.arrive_and_wait();
```

Cada thread t processa os núcleos com índices `t, t+COMPUTE_THREADS, t+2×COMPUTE_THREADS, ...`

Com NUM_CORES=16, COMPUTE_THREADS=7:
```
t=0 → núcleos 0, 7, 14   (3 núcleos × ~129.600 pixels / núcleo)
t=1 → núcleos 1, 8, 15   (3 núcleos)
t=2 → núcleos 2, 9       (2 núcleos)
t=3 → núcleos 3, 10      (2 núcleos)
t=4 → núcleos 4, 11      (2 núcleos)
t=5 → núcleos 5, 12      (2 núcleos)
t=6 → núcleos 6, 13      (2 núcleos)
```

**CPU ao longo da Fase 1:**
- Todas as 7 threads: 100% útil (executando instruções RV32I reais)
- Esta é a fase de maior utilidade — todos os cores fazendo trabalho produtivo

**Total no `top`:** 700% de trabalho real

```cpp
        // ── Fase 2: publicação (apenas líder) ────────────────────────
        if (t == 0) {
            swap_buffers();
            { /* mutex: publica para a thread da tela */ }
            shared.cv.notify_one();
            processing_units[0].load_program(geom_prog_);  // restaura geometry
        }
        barrier_pub_.arrive_and_wait();
    }
}
```

Após a Fase 2 e o `barrier_pub_`, todas as threads voltam ao início do `while`. Se `running_` é falso, saem. Caso contrário, recomeçam a Fase 0.

### Timeline visual de um frame

```
Tempo →
         Fase 0 (geometry)    Fase 1 (fragment, ~98% do tempo)   Fase 2
t=0  ██████████████████████│████████████████████████████████████│████████
t=1  ░░░░░░░░░░░░░░░░░░░░░│████████████████████████████████████│░░░░░░░░
t=2  ░░░░░░░░░░░░░░░░░░░░░│████████████████████████████████████│░░░░░░░░
t=3  ░░░░░░░░░░░░░░░░░░░░░│████████████████████████████████████│░░░░░░░░
t=4  ░░░░░░░░░░░░░░░░░░░░░│████████████████████████████████████│░░░░░░░░
t=5  ░░░░░░░░░░░░░░░░░░░░░│████████████████████████████████████│░░░░░░░░
t=6  ░░░░░░░░░░░░░░░░░░░░░│████████████████████████████████████│░░░░░░░░

████ = trabalho útil   ░░░░ = spin (100% CPU mas "esperando")
```

O geometry shader é sequencial e rápido (8 vértices × projeção + 12 arestas Bresenham). O fragment shader processa N pixels divididos entre núcleos — é a fase dominante. Em frames com N=1.000.000 pixels, o geometry shader ocupa < 1% do tempo total, então as threads t=1..6 ficam em spin por apenas uma fração mínima do frame.

### O que o sistema operacional vê

O SO Linux usa `struct task_struct` para cada thread. Threads em spin estão no estado `TASK_RUNNING` — elegíveis para o scheduler, executando instruções a cada quantum de tempo. O `top` calcula "CPU%" como `tempo em TASK_RUNNING / tempo total`. Uma thread que nunca dorme tem `tempo em TASK_RUNNING = tempo total` → 100%.

O agendador CFS (Completely Fair Scheduler) do Linux aloca fatias de tempo de forma justa. Com 7 threads e 7+ cores físicos, cada thread recebe seu próprio core e nunca é preemptada por outras — exatamente o regime de alta performance desejado.

### Comparação: antigo vs novo modelo

| Característica | Modelo antigo (std::async) | Modelo novo (spin pool) |
|---|---|---|
| Threads por frame | criadas e destruídas a cada frame | 7 threads persistentes, jamais destruídas |
| CPU entre frames | ~0% (threads dormem) | 100% contínuo |
| Latência de início de frame | ~0.5–2 ms (criação de thread) | ~nanossegundos (spin já ativo) |
| Overhead de sincronização | futex_wait / futex_wake (syscalls) | spin puro em userspace |
| Throughput | limitado por criação de threads | máximo do hardware |
| Consumo de energia | baixo entre frames | alto e constante |

Para um emulador de GPU que maximiza frames/segundo, o modelo novo é superior. Para uma aplicação que processa jobs esporadicamente, o modelo antigo conservaria energia.

---

## 24. RV32ICore::reset() — reinicialização eficiente sem recarga

### O problema sem reset()

Depois de `execute()`, o PC do núcleo está na instrução ECALL (ou além do fim do programa). Uma segunda chamada a `execute()` retornaria imediatamente sem executar nada (`word_idx >= instruction_memory.size()`). Para reutilizar o núcleo, era necessário `load_program()`, que copia o vetor inteiro de instruções — caro se feito 16 vezes por frame.

### A solução: reset()

```cpp
void RV32ICore::reset() {
    pc = 0;
    std::fill(std::begin(registers), std::end(registers), 0u);
    registers[10] = mhartid;  // restaura a0 = hart ID
}
```

Mantém `instruction_memory` intacta. Apenas zera PC e registradores. Custo: 32 × 4 = 128 bytes escritos. Comparado ao `load_program()` (cópia de centenas ou milhares de words de instrução), `reset()` é ordens de magnitude mais rápido.

**Uso no spin loop:**
- Núcleos 1–15 (fragment shader permanente): `reset()` + `execute()` a cada frame.
- Núcleo 0 (geometry → fragment alternado):
  - Fase 0: `reset()` + `execute()` (roda geometry, instrução_memory permanece geom_prog)
  - Transição 0→1: `load_program(frag_prog_)` (copia frag, reseta estado — 1× por frame)
  - Fase 1: executa (fragment já carregado)
  - Transição 2→próx. Fase 0: `load_program(geom_prog_)` (restaura geometry — 1× por frame)

Apenas 2 cópias de program por frame (só para o núcleo 0), versus 16 cópias no modelo antigo.

---

## 25. Correção de bugs de resolução — independência de potência de 2

### O problema original

Dois shaders assumiam que certas dimensões eram potências de 2:

**Fragment shader** — cálculo de base_addr:
```cpp
// ERRADO: assume ppc = 2^k
uint32_t shift = floor(log2(ppc));
SLL t1, mhartid, shift  // t1 = mhartid << shift ≠ mhartid × ppc se ppc ≠ 2^k
```

Para N=1920×1080=2.073.600 e 16 núcleos: ppc = 129.600 ≠ 2^k. O shift calculado era 16 (2^16=65.536 < 129.600). Resultado: núcleo k começava a processar a partir do pixel k×65.536 em vez de k×129.600 — sobreposições e lacunas.

**Geometry shader** — endereço do pixel no Bresenham:
```cpp
// ERRADO: assume W = 2^k
SLLI x30, y, log2W   // x30 = y × 2^floor(log2(W))
ADD  x30, x30, x     // addr = N + y × 2^floor(log2(W)) + x ← linha errada se W ≠ 2^k
```

Para W=1920: log2W calculado = 10 (2^10=1024 ≠ 1920). Pixel (x, y) era gravado no endereço N + y×1024 + x em vez de N + y×1920 + x — Bresenham desenhava nas linhas erradas da edge mask.

**Por que funcionava com W=128?**
128 = 2^7, ppc = 128²/64 = 256 = 2^8. Ambos potências de 2 → shift exato → bugs mascarados.

### A correção

**Fragment shader** — usar MUL em vez de SLL:
```cpp
// CORRETO: MUL funciona para qualquer ppc
MUL t1, mhartid, t0   // t1 = mhartid × ppc (t0 = ppc, carregado no prólogo)
```
Troca 2 instruções (ADDI+SLL) por 1 instrução (MUL). Os offsets dos branches no loop não mudam porque o loop em si continua com o mesmo número de instruções (a redução aconteceu no prólogo).

**Geometry shader** — carregar W em registro e usar MUL:

No prólogo do shader (executado uma vez):
```cpp
// Emitido em build_geometry_shader() via RV32Asm:
ADDI x9, x0, W    // x9 = W (para W < 2048, cabe em ADDI imediato de 12 bits)
                   // para W ≥ 2048: LUI x9, (W>>12) + ADDI x9, x9, (W&0xFFF)
```

No loop Bresenham (substituindo SLLI):
```cpp
// CORRETO: MUL com W no registrador x9
MUL x30, y, x9    // x30 = y × W (exato para qualquer W)
ADD x30, x30, x   // x30 = y × W + x
ADD x30, x30, x4  // x30 = N + y × W + x  ← endereço correto
```

O número de instruções do bloco Bresenham permanece 39 (SLLI→MUL: mesma largura). Os offsets de todos os branches dentro do bloco são preservados.

### Restrições restantes

- `ADDI` tem imediato de 12 bits signed (max 2047). Parâmetros emitidos como ADDI: `proj_W = CUBE_SCALE × W`, `half_W = W/2`. Para W=1920 com CUBE_SCALE≤1.0: proj_W≤1920 < 2047 ✓. Para W≥4580 com CUBE_SCALE≥0.5: proj_W≥2290 → overflow (precisaria de LUI+ADDI).
- `SLTIU x30, x, W` (bounds check no Bresenham) também tem imediato de 12 bits. Para W≥2048: necessitaria abordagem alternativa.
- `N / NUM_CORES` deve ser inteiro. Para N=1920×1080=2.073.600 e NUM_CORES=16: ppc=129.600 ✓ (divisão exata).

---

## 26. Controles de config.h — referência completa

```cpp
// ── Framebuffer ───────────────────────────────────────────────────────
constexpr int FB_W  = 1000;  // largura interna do framebuffer em pixels
constexpr int FB_H  = 1000;  // altura interna do framebuffer em pixels
constexpr int SCALE = 1;     // fator de escala inicial da janela SDL
```

`FB_W` e `FB_H` definem a resolução de renderização **interna** dos shaders. A janela SDL pode ser redimensionada independentemente — `SDL_RenderSetLogicalSize(ren, fb_w, fb_h)` garante que o conteúdo seja sempre esticado para preencher a janela, mantendo proporção.

```cpp
// ── GPU ───────────────────────────────────────────────────────────────
constexpr int NUM_CORES       = 16;  // núcleos RV32I
constexpr int COMPUTE_THREADS =  7;  // threads de cálculo no spin pool
```

Relação entre `NUM_CORES` e `COMPUTE_THREADS`: os núcleos são distribuídos por stride (núcleo i → thread i % COMPUTE_THREADS). Para máximo balanceamento, `NUM_CORES` divisível por `COMPUTE_THREADS` ou com diferença de até 1 entre os grupos.

```cpp
// ── Tamanho do cubo ───────────────────────────────────────────────────
constexpr float CUBE_SCALE = 0.45f;
```

Controla `proj_W = round(CUBE_SCALE × FB_W)` e `proj_H = round(CUBE_SCALE × FB_H)` no geometry shader. Valores práticos: 0.15 (cubo pequeno, muito espaço ao redor) a 0.95 (cubo enorme, arestas quase fora da tela). Compilado como imediato ADDI nas instruções de projeção.

```cpp
// ── Velocidade de rotação ─────────────────────────────────────────────
constexpr int ROT_STEP    = 1;    // passos de angle_idx por frame (eixo Y)
constexpr int ROT_X_RATIO = 154;  // velocidade eixo X em 256avos do eixo Y
```

`ROT_STEP`: somado ao `angle_idx` atômico a cada frame pelo worker t=0. A LUT tem 256 entradas (360° completos). `ROT_STEP=1` → 1 passo = 360/256 ≈ 1,4°/frame. `ROT_STEP=16` → ~22°/frame = volta completa em ~16 frames.

`ROT_X_RATIO`: emitido como imediato ADDI no prólogo do geometry shader — `ax_idx = (angle_idx × ROT_X_RATIO) >> 8`. A divisão por 256 (SRAI 8) faz o redimensionamento Q8.8. Com `ROT_X_RATIO=128`: eixo X gira à metade da velocidade do eixo Y. Com `ROT_X_RATIO=0`: sem rotação no eixo X (cubo gira apenas em Y). Deve caber em imediato ADDI de 12 bits (0–2047).

---

## 27. Janela redimensionável — SDL_RenderSetLogicalSize

```cpp
// Em Janela::Janela():
win = SDL_CreateWindow(title, ...,
    fb_w * scale, fb_h * scale,
    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);  // ← flag adicionada

ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
SDL_RenderSetLogicalSize(ren, fb_w, fb_h);     // ← tamanho lógico fixo
```

**`SDL_WINDOW_RESIZABLE`**: permite que o usuário arraste as bordas da janela. Sem essa flag, tentativas de redimensionar são ignoradas pelo gerenciador de janelas.

**`SDL_RenderSetLogicalSize(ren, fb_w, fb_h)`**: define o tamanho lógico do renderer. Quando a janela tem tamanho diferente do lógico, SDL2 calcula automaticamente:
- A escala (`window_w / fb_w`, `window_h / fb_h`)
- O offset para manter proporção (barras pretas se proporções diferem)
- `SDL_RenderCopy` com `nullptr` como destino preenche o espaço lógico, não o físico

Sem `SDL_RenderSetLogicalSize`, `SDL_RenderCopy` usaria o tamanho físico atual da janela — correto se as proporções coincidem, mas a textura seria esticada de forma diferente do esperado se a janela não mantiver a proporção de FB_W×FB_H.

O framebuffer interno permanece sempre FB_W×FB_H — a resolução de renderização não muda ao redimensionar. O cubo mantém sua qualidade visual independente do tamanho da janela.

---

## Resumo das dependências entre módulos

```
config.h ──────────────────────────────► todos os módulos
vram_layout.h ─────────────────────────► geom_shader, gpu, main
VRAM.h ────────────────────────────────► RV32ICore, gpu
RV32Asm.h ─────────────────────────────► geom_shader, frag_shader
RV32ICore.h/.cpp ──────────────────────► gpu
geom_shader.h/.cpp ────────────────────► main
frag_shader.h/.cpp ────────────────────► main
gpu.h/.cpp ────────────────────────────► main
janela.h/.cpp ─────────────────────────► main
```

### Sequência completa de chamadas por frame

```
main()
  ├─ gpu.set_angle(idx)
  │    └─ back_buffer.memory[2N] = idx
  ├─ gpu.load_geometry(geom_prog, 0)
  │    └─ processing_units[0].load_program()
  │         ├─ instruction_memory = geom_prog
  │         └─ pc=0, registers={0,…,mhartid}
  ├─ gpu.dispatch_geometry(0)
  │    └─ processing_units[0].execute()
  │         └─ loop: step() → fetch→decode→execute → step() → ...
  │              └─ lê/escreve back_buffer via vram_ref
  ├─ gpu.load_program(frag_prog)   ← reseta os 64 núcleos
  ├─ gpu.dispatch_frame()
  │    ├─ batch 0: async(núcleos 0..7) → future.get()
  │    ├─ batch 1: async(núcleos 8..15) → future.get()
  │    ├─ ...
  │    ├─ batch 7: async(núcleos 56..63) → future.get()
  │    └─ swap_buffers(): front ← color(back), zera color(back)
  └─ janela.draw(gpu.get_pixels())
       ├─ converte RRGGBBAA → ARGB8888
       ├─ SDL_UpdateTexture()   ← copia pixels para GPU real
       ├─ SDL_RenderCopy()      ← estica 128×128 → 640×640
       └─ SDL_RenderPresent()   ← exibe na tela
```

---

## 28. Benchmark — Metodologia e Objetivo

### Por que um benchmark headless?

O benchmark (`benchmark.cpp`) executa o mesmo pipeline de renderização do GPU-V
**sem** janela SDL2, sem thread da tela e sem sincronização de monitor. Isso elimina
três fontes de variância externas ao desempenho do motor de cálculo:

1. **V-sync / frame limiter** — o SDL2 respeita o refresh rate do monitor (60 Hz no
   modo não-acelerado); o benchmark não tem esse teto.
2. **Tempo de `SDL_UpdateTexture` + `SDL_RenderPresent`** — cópia de pixels para a GPU
   real e flip do framebuffer consomem tempo proporcional à resolução e à latência do
   driver gráfico.
3. **Thread de eventos** — `poll_events()` + `wait_for(16ms)` adicionam latência não
   determinística ao pipeline.

Sem essas interferências, o FPS medido reflete **exclusivamente** o desempenho do
loop `worker()`: geometry shader + fragment shaders + `swap_buffers()`.

### Estrutura do benchmark

```
Para cada (cores, threads, W, H):
  1. Cria BenchmarkGPU(cores, threads, W, H)
     - aloca VRAM, inicializa LUTs, compila shaders
  2. Lança num_threads workers em spin loop
  3. Dorme 500 ms  ← aquecimento: CPU freq ramp-up, branch predictor
  4. Zera frame_count, grava t0
  5. Dorme 3000 ms ← janela de medição
  6. running = false  → workers terminam no final do frame atual
  7. Join de todas as threads
  8. Grava t1
  9. FPS    = frame_count / (t1 - t0)
     Mpix/s = FPS × W × H / 1e6
```

### O que "1 frame" inclui

Cada unidade contada em `frame_count` abrange:

| Fase | Executor | Custo dominante |
|------|----------|-----------------|
| Geometry (Fase 0) | Thread 0 apenas | Loop de limpeza O(N) + Bresenham |
| Barrier `geom` | Todos os N threads | Spin até o último chegar |
| Fragment (Fase 1) | Todos os N threads | N/cores pixels × ~9 instr/pixel |
| Barrier `frag` | Todos os N threads | Spin até o último chegar |
| Swap + publish (Fase 2) | Thread 0 apenas | `std::copy` + `std::fill` de N palavras |
| Barrier `pub` | Todos os N threads | Spin antes do próximo frame |

### Plataforma de teste

```
CPU:             AMD Ryzen 5 3500U (Zen+, mobile)
Núcleos físicos: 4
HW threads:      8 (SMT 2-way)
Frequência:      2.1 – 3.7 GHz (boost)
Cache L1d:       4 × 32 KB
Cache L2:        4 × 512 KB = 2 MB total
Cache L3:        4 MB (compartilhado)
SO:              Linux 6.8
Compilador:      GCC 13, -O2 -march=native
Modo:            headless, 500 ms aquecimento + 3000 ms medição
Total de testes: 24 (4 secoes)
```

---

## 29. Seção 1 — Escalabilidade de Threads de Cálculo

**Configuração fixa:** 14 núcleos RV32I, resolução 1000×1000.
**Variável:** `COMPUTE_THREADS` de 1 a 14.

### Resultados

```
Threads | Frames |    FPS |  Mpix/s | Eficiencia | Speedup
--------|--------|--------|---------|------------|---------
      1 |     27 |   8,85 |    8,85 |    100,0%  |  1,00x
      2 |     31 |  10,33 |   10,33 |    116,7%  |  1,17x
      4 |     47 |  15,35 |   15,35 |    173,5%  |  1,73x   <- OTIMO
      7 |     36 |  11,70 |   11,70 |    132,3%  |  1,32x
     14 |     20 |   6,48 |    6,48 |     73,2%  |  0,73x
```

**Eficiência** = FPS_N / (FPS_1 × N) × 100%
**Speedup** = FPS_N / FPS_1

### Análise linha a linha

#### 1 thread → 2 threads (+16,7%, eficiência 116,7%)

Com 1 thread, a CPU física está sendo usada por 1 core do Ryzen. Ao adicionar
uma segunda thread, o **SMT (Simultaneous Multithreading)** do Ryzen entra em
ação: as 2 threads compartilham os recursos do mesmo core físico (unidades de
execução, caches) mas os pipelines são alimentados de forma mais contínua. O
ganho de +16,7% acima do speedup linear 1,0x → 2,0x pode parecer contraditório,
mas na realidade o 1-thread-apenas era **subótimo**: com uma única thread em
spin contínuo, a CPU tinha pockets de inatividade no pipeline (dados esperando
da VRAM). A segunda thread preenche esses vácuos.

#### 2 threads → 4 threads (+48,6%)

Este é o salto mais expressivo. Com 4 threads de cálculo:

```
Total de threads ativas no SO:
  4 workers (spin 100%)
+ 1 thread da tela (bloqueada em condvar 16 ms -> quase inativa)
+ 1 thread main (bloqueada em join -> inativa)
= ~6 threads no agendador

Hardware disponivel: 8 HW threads (4 cores x 2 SMT)
Threads realmente competindo por CPU: 4
```

Cada worker 1-3 executa o fragment shader enquanto o worker 0 executa o
geometry shader. Como o total de 4 threads ativas <= 4 núcleos físicos, **cada
thread tem dedicação exclusiva a um core físico** sem troca de contexto. Isso
maximiza o throughput de cache (cada core tem seu próprio L1d e L2).

#### 4 threads → 7 threads (-23,8%)

A contagem de threads ativas vai para ~7. O Ryzen 5 3500U tem apenas 4 cores
físicos. Para servir 7 threads em spin contínuo, o agendador Linux precisa
**multiplexar temporalmente** — cada core físico alterna entre 2 threads ao
longo do tempo (via SMT ou via preempção real dependendo da carga).

O problema crítico: o **worker 0** (que executa o geometry shader, a fase
serial) agora compete por CPU com 3 ou 6 outros workers que estão em spin nas
barreiras. O worker 0 recebe menos tempo de CPU proporcional -> a fase serial
fica mais lenta -> todo o pipeline degrada.

```
Analogia:
  Com 4 threads: worker 0 recebe ~25% do tempo total de 4 cores = 1 core dedicado
  Com 7 threads: worker 0 recebe ~14% do tempo total -> nao tem core dedicado
```

#### 7 threads → 14 threads (-44,6% vs 4 threads)

Com 14 workers em spin, o total chega a ~16 threads para 8 HW threads. O
agendador precisa time-slice com overhead de troca de contexto (~1.000 ciclos
cada). Além disso:

- **Contenção de cache**: 14 threads em spin executando `gen.load()` na mesma
  linha de cache da `SpinBarrier` geram 14 fluxos de tráfego de coerência
  de cache (protocolo MESI). Mesmo com `PAUSE`, isso satura o barramento L3.
- **Thrashing de TLB**: 14 stacks separados fazendo acesso aleatório à VRAM
  polui o iTLB e dTLB dos 4 cores.
- **O worker 0 recebe ~7% do tempo** -> geometry extremamente lento.

O resultado (6,48 FPS) é **pior que 1 thread** (8,85 FPS), demonstrando que
adicionar threads além da capacidade física do hardware não apenas não ajuda,
mas destrói ativamente o desempenho.

### Por que a eficiência supera 100% com 2 e 4 threads?

Eficiência > 100% significa que o speedup foi maior que o esperado pelo
modelo linear simples. Isso acontece porque:

1. **O baseline de 1 thread é ineficiente**: o pipeline do core físico tem
   latências de memória não mascaradas com apenas 1 thread.
2. **SMT oculta latências**: com 2+ threads, quando uma thread espera por
   dados da VRAM (cache miss -> 100+ ciclos de latência), a outra thread
   avança no seu trabalho. O core físico nunca fica ocioso.
3. **Pipeline mais cheio**: instruções de múltiplas threads podem ser emitidas
   no mesmo ciclo se usarem unidades de execução diferentes (ex: ALU inteira
   + unidade de load/store).

---

## 30. Seção 2 — Escalabilidade de Núcleos RV32I

**Configuração:** resolução 1000×1000, `threads = min(cores, 7)`.
**Variável:** `NUM_CORES` de 1 a 28.

### Resultados

```
Nucleos | Threads | Frames |    FPS |  Mpix/s | Speedup vs 1 core
--------|---------|--------|--------|---------|-------------------
      1 |       1 |     34 |  11,22 |   11,22 |        1,00x
      2 |       2 |     34 |  10,96 |   10,96 |        0,98x
      4 |       4 |     40 |  13,18 |   13,18 |        1,17x
      8 |       7 |     33 |  10,72 |   10,72 |        0,96x
     14 |       7 |     31 |  10,21 |   10,21 |        0,91x
     28 |       7 |     36 |  11,68 |   11,68 |        1,04x
```

### O invariante fundamental

O trabalho total do **fragment shader é invariante ao número de núcleos**:

```
total_pixels = N = W x H = 1.000.000

Com C nucleos, cada nucleo processa:
  ppc = N / C pixels
  executa ~9 instrucoes por pixel
  total por nucleo = 9 x ppc = 9N/C

Em 7 threads com C nucleos, cada thread processa ceil(C/7) nucleos:
  trabalho por thread = ceil(C/7) x 9N/C ≈ 9N/7  (independe de C!)
```

Isso significa que **dobrar C não muda o trabalho por thread**. O FPS deveria
ser constante conforme C cresce --- e é: variação de apenas 10,21 a 13,18 FPS.

### Por que há variação mesmo com trabalho constante?

**1. Custo de `reset()`**: por frame, o worker faz `reset()` em C núcleos
(cada reset zera 32 registradores = 128 bytes).

```
C= 1: reset =  1 x 32 escritas =   32 registradores/frame
C= 4: reset =  4 x 32 escritas =  128 registradores/frame
C=14: reset = 14 x 32 escritas =  448 registradores/frame
C=28: reset = 28 x 32 escritas =  896 registradores/frame
```

**2. O geometry shader não muda**: independente de C, o geometry shader sempre
executa no núcleo 0 com o mesmo programa de 541 instruções + N iterações de
limpeza. Ele domina o tempo serial.

**3. Número de iterações do fragment loop**: com C=1, o único núcleo processa
1.000.000 pixels em sequência. Com C=28, cada núcleo processa 35.714 pixels.

### Conclusão da seção 2

Variar `NUM_CORES` de 1 a 28 produz menos de 30% de variação no FPS. Isso
confirma que o **gargalo não está no fragment shader** (paralelo e invariante)
mas no **geometry shader** (serial, O(N)). O número de núcleos RV32I é um
parâmetro de granularidade, não de throughput global.

---

## 31. Seção 3 — Impacto da Resolução

**Configuração fixa:** 14 núcleos, 7 threads.
**Variável:** resolução W×H.

### Resultados

```
Resolucao    |   Pixels |    FPS |  Mpix/s | VRAM total | Cabe no L3?
-------------|----------|--------|---------|------------|------------
  256x256    |   65.536 |   73,5 |     4,8 |   ~0,5 MB  | Sim (L2)
  512x512    |  262.144 |   38,5 |    10,1 |   ~2,0 MB  | Sim (L3)
 1000x1000   |1.000.000 |   11,0 |    11,0 |   ~7,6 MB  | Nao (>L3)
 1920x1080   |2.073.600 |    6,0 |    12,5 |  ~15,8 MB  | Nao (>>L3)
```

A VRAM total = `(2N + 553) × 4 bytes`.

### A tendência oposta: FPS cai, Mpix/s sobe

Cada frame tem um custo fixo **independente de N**:

```
Componente                    | Custo    | Escala com N?
------------------------------|----------|--------------
Rotacao 8 vertices (geom)     | ~368 ins | Nao
Projecao perspectiva (geom)   | ~184 ins | Nao
12 blocos Bresenham (geom)    | ~600 ins | Parcial
3x barrier arrive_and_wait    | ~50 cicl | Nao
load_program 2x (nucleo 0)    | ~1 us    | Nao
condvar notify_one            | ~1 us    | Nao

Componente                    | Custo    | Escala com N?
------------------------------|----------|--------------
Loop de limpeza da edge mask  | 3N instr | Sim (linear)
Fragment shader (14 nucleos)  | 9N instr | Sim (linear)
swap_buffers (copy+fill)      | 2N words | Sim (linear)
```

Em **baixa resolução** (256×256, N=65.536), o custo fixo representa uma fração
grande do tempo total de frame. Em **alta resolução** (1920×1080, N=2.073.600),
o custo linear domina completamente. Logo, a eficiência por pixel (Mpix/s)
**melhora** conforme N cresce porque o overhead fixo é amortizado.

#### Estimativa numérica do overhead relativo

```
Overhead fixo por frame: ~27.000 "unidades de custo"
Custo linear por N:      ~5,64N unidades

Fracao de overhead fixo:
  256x256:    27k / (27k + 5,64x65.536)    ≈ 6,8%
  512x512:    27k / (27k + 5,64x262.144)   ≈ 1,8%
  1000x1000:  27k / (27k + 5,64x1.000.000) ≈ 0,5%
  1920x1080:  27k / (27k + 5,64x2.073.600) ≈ 0,2%
```

Assim em 256×256 o sistema "desperdiça" ~7% em overhead, mas em 1920×1080
apenas ~0,2%. Por isso o Mpix/s cresce de 4,8 para 12,5.

#### Efeito da hierarquia de cache

```
Resolucao  | VRAM total | Situacao no cache
-----------|------------|-------------------------------------------
256x256    |  ~0,5 MB   | Cabe no L2 (2MB) -> hit rate alto, rapido
512x512    |  ~2,0 MB   | Cabe no L3 (4MB) -> L2 miss, L3 hit
1000x1000  |  ~7,6 MB   | Nao cabe no L3 -> L3 miss -> RAM (~60ns)
1920x1080  | ~15,8 MB   | Muito maior que L3 -> RAM bound
```

Com 1920×1080, o loop de limpeza da edge mask (2M escritas) e o swap_buffers
(2M cópias) causam **L3 cache thrashing**. Isso é por que o Mpix/s cresce
de forma sublinear (não dobra de 1000×1000 para 1920×1080 apesar dos pixels
dobrarem: 11,0 vs 12,5 Mpix/s, apenas +14%).

---

## 32. Seção 4 — Grade Cores × Threads

**Configuração:** resolução 1000×1000.
**Variáveis:** `NUM_CORES` ∈ {4, 8, 16} e `COMPUTE_THREADS` ∈ {1, 2, 4, 8, 16}.

### Resultados (FPS)

```
              |  thr=1  |  thr=2  |  thr=4  |  thr=8  |  thr=16
--------------|---------|---------|---------|---------|--------
  4 nucleos   |  11,0   |  12,2   |  11,9   |    --   |    --
  8 nucleos   |  10,4   |   --    |  13,3   |  10,0   |    --
 16 nucleos   |   --    |  11,6   |   --    |  10,6   |   5,7
```

**Melhor absoluto: 8 núcleos / 4 threads = 13,3 FPS**

### Padrão identificado

```
threads= 1 -> melhor: 4 nucleos, 11,0 FPS
threads= 2 -> melhor: 4 nucleos, 12,2 FPS
threads= 4 -> melhor: 8 nucleos, 13,3 FPS  <- MELHOR ABSOLUTO
threads= 8 -> melhor: 16 nucleos, 10,6 FPS
threads=16 -> melhor: 16 nucleos,  5,7 FPS
```

### Por que 8 núcleos supera 4 e 16 com 4 threads?

Com 4 threads de cálculo (stride=4):

```
Com 4 nucleos/4 threads:  cada thread executa 1 nucleo  -> ppc = 250k pixels
Com 8 nucleos/4 threads:  cada thread executa 2 nucleos -> ppc = 125k pixels x 2
Com 16 nucleos/4 threads: cada thread executa 4 nucleos -> ppc = 62,5k pixels x 4
```

Com 4 núcleos, o fragment shader processa 250.000 pixels por invocação — loop
mais longo, melhor utilização de cache por kernel. Com 16 núcleos, cada
`execute()` processa apenas 62.500 pixels, e o overhead de `reset()` e
`load_program` (16 chamadas vs 4) pesa mais relativamente. O ponto de 8 núcleos
equilibra tamanho do loop vs overhead de inicialização.

### Degradação severa com 16 threads (5,7 FPS)

Com 16 workers em spin + 2 auxiliares = 18 threads para 8 HW threads:

```
Threads de SO: 18
HW threads:     8
Ratio:         2,25x -> cada HW thread serve 2,25 threads de SO em media

Worker 0 (geometry, serial) recebe: 1/18 ≈ 5,5% do tempo total
vs ideal com 4 threads:             1/6  ≈ 16,7% do tempo total

Degradacao do worker 0: 16,7% -> 5,5% = -67% -> geometry fica 3x mais lento
```

---

## 33. Lei de Amdahl — Análise Quantitativa Completa

### Enunciado

Gene Amdahl (1967) formulou o limite teórico de speedup:

```
             1
S(n) = ─────────────
         s + (1-s)/n

Onde:
  S(n) = speedup com n processadores
  s    = fracao serial (0 <= s <= 1)
  1-s  = fracao paralelizavel
  n    = numero de processadores

Limite: quando n -> infinito, S_max = 1/s
```

### Identificação da fração serial no GPU-V

A fase serial é a Fase 0 (geometry shader), executada apenas pelo Worker 0.

```
Componente                      | Instrucoes executadas | Paralelo?
--------------------------------|-----------------------|----------
Leitura de angle_idx (1 LW)     |              1        | Nao
Carga sin/cos (4 LW)            |              4        | Nao
Rotacao 8 vertices (46x8 instr) |            368        | Nao
Projecao 8 vertices (~23x8)     |            184        | Nao
Loop de limpeza edge mask       |      3.000.000        | Nao (*)
12 blocos Bresenham (~500 instr)|         ~6.000        | Nao
Troca instruction_memory        |            ~50        | Nao
--------------------------------|-----------------------|----------
Total fase serial               |      ~3.006.600       |

(*) Poderia ser paralelizado — maior oportunidade de otimizacao
```

```
Componente                      | Instr. totais | Paralelo?
--------------------------------|---------------|----------
Fragment shader (14 nucleos)    |  ~9.000.000   | Sim
swap_buffers (copy + fill)      |  ~2.000.000   | Nao (*)
--------------------------------|---------------|----------
Total fase paralela             | ~11.000.000   |

(*) swap_buffers serial porque roda no worker 0 na Fase 2
```

### Cálculo do s teórico vs empírico

```
s_teorico = 3.006.600 / (3.006.600 + 11.000.000) ≈ 0,21

Mas o s medido empiricamente e maior. Ajuste pela dados:

Speedup medido (1->4 threads): 15,35 / 8,85 = 1,734x

Resolvendo para s:
  1,734 = 1 / (s + (1-s)/4)
  1,734 x (s + (1-s)/4) = 1
  1,734s + 0,4335(1-s) = 1
  1,734s + 0,4335 - 0,4335s = 1
  1,3005s = 0,5665
  s = 0,4356 ≈ 0,42
```

### Por que s empírico (0,42) > s teórico (0,21)?

A diferença (0,42 vs 0,21) reflete overheads reais não modelados:

```
Overhead extra de s (≈0,21 adicional):
  1. swap_buffers serial:         2M ops memória por frame (thread 0)
  2. SpinBarrier overhead:        14 atomics x 3 barreiras = 42 atomics/frame
  3. Contenção de cache MESI:     N threads fazendo load() no mesmo atômico
  4. load_program x2 por frame:   copy de vector (541 + 18 elementos)
  5. Preempção de OS:             agendador Linux CFS com timeslice de 1ms
```

### Tabela completa: predição vs medição

```
Threads | Speedup medido | Amdahl s=0,42 | Diferenca | Interpretacao
--------|----------------|---------------|-----------|-------------------
      1 |         1,000x |        1,000x |     0,0%  | baseline
      2 |         1,167x |        1,155x |    +1,0%  | SMT ajuda
      4 |         1,734x |        1,779x |    -2,5%  | fit excelente
      7 |         1,322x |        2,014x |   -34,4%  | saturacao HW threads
     14 |         0,732x |        2,204x |   -66,8%  | colapso total
```

Ate 4 threads: Amdahl modela bem. Acima: saturacao de hardware threads
(efeito nao modelado por Amdahl, que assume processadores ilimitados).

### Teto teórico e potencial de otimização

```
S_max atual = 1 / 0,42 = 2,38x

Com 1 thread baseline (8,85 FPS):
  Maximo teorico atual = 8,85 x 2,38 = 21,1 FPS
  Medido em 4 threads:  15,35 FPS = 72,6% do maximo teorico
```

Se o loop de limpeza fosse paralelizado entre as 7 threads:

```
Novo s_serial = (swap_buffers + barreiras + overhead) / total
             ≈ (2.000.000 + 50.000) / (2.050.000 + 9.000.000)
             ≈ 0,19

S_max_novo = 1 / 0,19 = 5,26x
Teto = 8,85 x 5,26 ≈ 46,5 FPS (vs 21,1 FPS atual)
```

Se swap_buffers tambem fosse paralelizado:

```
s_restante ≈ 0,03 (so overhead de barreiras e atomics)
S_max = 1 / 0,03 ≈ 33x
Teto = 8,85 x 33 ≈ 292 FPS (limitado pelo hardware real)
```

### Lei de Gustafson — escalonamento fraco

Gustafson (1988) pergunta: "e se o tamanho do problema crescer com os
processadores?" No GPU-V isso significa aumentar N junto com os threads:

```
1 thread,  256x256 (N=65k):    73,5 FPS,  4,8 Mpix/s
4 threads, 512x512 (N=262k):   38,5 FPS, 10,1 Mpix/s  (N ~4x maior)
7 threads, 1000x1000 (N=1M):   11,0 FPS, 11,0 Mpix/s  (N ~15x maior)
```

O Mpix/s cresce de 4,8 para 11,0 -- mais do que dobrou com resolucao 15x
maior. Isso confirma **escalonamento fraco favoravel**: ao aumentar N
proporcionalmente ao numero de threads, o throughput por pixel melhora
porque o overhead fixo por frame é diluído.

---

## 34. Conclusões dos Benchmarks e Recomendações

### Resumo dos 4 achados principais

```
Achado                            | Evidencia                    | Causa
----------------------------------|------------------------------|---------------------
Pico em 4 threads (15,35 FPS)     | Secao 1 (§29)                | 4 cores fisicos
Degradacao acima de 4 threads     | FPS: 15,35->11,70->6,48      | Saturacao HW threads
Nucleos adicionais nao ajudam     | FPS varia <30% com cores     | Trabalho fragment fixo
Mpix/s cresce com resolucao       | 4,8->12,5 Mpix/s             | Overhead fixo amortizado
s ~= 0,42 (fracao serial)         | Ajuste Amdahl                | Loop de limpeza O(N)
Teto atual: ~21 FPS em 1000x1000  | 1/0,42 x 8,85               | Lei de Amdahl
```

### Recomendação de COMPUTE_THREADS por hardware

```
Hardware                  | Nucleos fisicos | COMPUTE_THREADS recomendado
--------------------------|-----------------|-----------------------------
Laptop 2 cores / 4 HW    |        2        |  2
Desktop 4 cores / 8 HW   |        4        |  4  <- Ryzen 5 3500U: OTIMO
Desktop 6 cores / 12 HW  |        6        |  6
Desktop 8 cores / 16 HW  |        8        |  8
Workstation 16 cores      |       16        | 14-16
```

**Regra**: `COMPUTE_THREADS = núcleos_físicos` (NAO HW threads).
Usar o numero de HW threads (SMT) nao melhora porque as threads em spin
ja consomem 100% do slot SMT disponivel.

### Os dois gargalos identificados para trabalhos futuros

**Gargalo 1 — Loop de limpeza O(N) serial** (maior impacto):

```cpp
// Situacao atual no geometry shader (executado apenas no nucleo 0):
//   for x in [N, 2N): VRAM[x] = 0    <- N iteracoes seriais no shader

// Otimizacao proposta: worker loop paralelo antes da geometry
// (acrescentar na Fase 0 antes de barrier_geom):
size_t chunk = N / num_threads_;
size_t start = N + (size_t)t * chunk;
std::fill(back_buf_.memory.begin() + start,
          back_buf_.memory.begin() + start + chunk, 0u);
// remover o clear loop do geometry shader
// impacto esperado: s cai de 0,42 para ~0,10
//                  teto sobe de 21 FPS para ~88 FPS
```

**Gargalo 2 — swap_buffers serial** (impacto menor):

```cpp
// Situacao atual (apenas worker 0, Fase 2):
std::copy(back_buf_.memory.begin(), ..., front_buf_.memory.begin());
std::fill(back_buf_.memory.begin(), ..., 0u);

// Otimizacao proposta (todos os workers em paralelo, Fase 2):
size_t chunk = N / num_threads_;
size_t base  = (size_t)t * chunk;
std::copy(back_buf_.memory.begin()  + base,
          back_buf_.memory.begin()  + base + chunk,
          front_buf_.memory.begin() + base);
std::fill(back_buf_.memory.begin()  + base,
          back_buf_.memory.begin()  + base + chunk, 0u);
// impacto esperado: elimina 2M ops memória do critical path serial
```

### benchmark.cpp como ferramenta de regressão de desempenho

O benchmark pode ser usado antes de cada commit para detectar regressões:

```bash
# Antes de mudança
./benchmark --quick 2>&1 | grep FPS > baseline.txt

# Após mudança
make benchmark && ./benchmark --quick 2>&1 | grep FPS > after.txt

# Diff de FPS
paste baseline.txt after.txt | awk '{
  split($0, a); old=a[1]; new=a[5];
  pct=(new-old)/old*100;
  printf "%-30s %6.1f -> %6.1f FPS  (%+.1f%%)\n", "Test:", old, new, pct
}'
```

Qualquer regressão acima de 5% em alguma configuração indica problema
de desempenho introduzido pela mudança.

---

## 35. Resultados Finais — Benchmark 2026-06-04 e Artigo IEEE

> **Nota sobre as seções 29–34**: os dados ali apresentados foram coletados em
> uma versão anterior do benchmark onde o **loop de limpeza da edge mask** ainda
> era executado na fase serial (geometry shader, worker 0). Nessa versão, o loop
> `for x in [N, 2N): VRAM[x] = 0` rodava com N = 1.000.000 iterações de forma
> serial a cada frame, dominando completamente a fração serial s.
>
> No benchmark definitivo (2026-06-04), esse loop foi **movido para a Fase 2
> (paralela)**, dividido entre todas as compute threads. Resultado: a fase serial
> caiu de O(N) para O(1) — apenas a interpretação do geometry shader (~536
> instruções RV32I) e o swap de buffers permanecem seriais. O perfil de
> desempenho mudou completamente.

---

### A otimização que mudou tudo: edge mask clear → Fase 2

| Versão | Fase de limpeza | Custo serial | FPS base (1 thread) |
|--------|-----------------|--------------|---------------------|
| Antiga | Geometry (serial) | O(N) = 3M instruções RV32I | ~9 FPS |
| Final  | Fragment (paralela) | O(N/threads) por thread | ~21 FPS |

Com a limpeza na Fase 2 paralela, cada thread limpa apenas `N/T` pixels, e o
resultado é dividido pelo número de threads — exatamente como o fragment shader.
A fase serial ficou com apenas o geometry shader (~536 instruções RV32I a
interpretar) e o swap de ponteiros de buffer.

---

### Seção 1 — Escalabilidade de Threads (dados corrigidos)

Configuração: 14 núcleos RV32I, 1000×1000, variando COMPUTE_THREADS.

```
Threads | Frames | FPS    | Mpix/s | Eficiência
--------|--------|--------|--------|------------
1       | 65     | 21,39  | 21,39  | 100,0%
2       | 66     | 21,86  | 21,86  | 102,2%   <- SMT: sem ganho real
3       | 111    | 36,94  | 36,94  | 172,7%   <- SALTO: 2o nucleo fisico
4       | 110    | 36,49  | 36,49  | 170,6%
5       | 94     | 31,26  | 31,26  | 146,2%
6       | 120    | 39,80  | 39,80  | 186,1%   <- PICO: 6 workers + 2 sys = 8 HW slots
7       | 113    | 37,53  | 37,53  | 175,5%   <- degradação começa
8       | 105    | 34,94  | 34,94  | 163,3%   <- 10 threads tentando por 8 slots
10+     | --     | SKIP   | SKIP   | oversubscribed
```

**Por que o pico está em 6 threads, não em 4 (como na versão anterior)?**

Com a limpeza paralela, a Fase 2 agora tem muito mais trabalho paralelo
(N pixels a limpar + N pixels a processar = 2N operações por thread em paralelo).
Com mais carga paralela, o hardware consegue saturar mais threads eficientemente.
A fórmula de saturação é: `workers + threads_sistema ≤ HW_threads`. Com 6 workers
+ 1 thread principal + 1 thread de tela = 8 threads totais = exatamente o limite
do Ryzen 5 3500U. Em 7+, o SO precisa multiplexar threads em spin ativo,
degradando o desempenho.

**O que acontece entre 1 e 2 threads (ganho mínimo)?**

Workers 0 e 1 estão ligados ao mesmo núcleo físico via SMT (Simultaneous
Multithreading). O núcleo físico tem um único conjunto de unidades de execução
— os dois SMT compartilham recursos. Portanto, ter 2 threads SMT não dobra a
capacidade: as duas threads competem pelos mesmos recursos de execução.

**O salto de 2 → 3 threads (de 21,9 para 36,9 FPS)?**

Worker 2 é alocado no segundo núcleo físico. Agora há paralelismo real entre dois
núcleos físicos distintos. O Worker 0 faz geometry + Fase 1 isolation enquanto
Workers 1-2 fazem fragment em paralelo real.

---

### Seção 2 — Escalabilidade de Núcleos RV32I (dados corrigidos)

Configuração: 1000×1000, `threads = min(cores, 7)`.

```
Cores | Threads | FPS    | Mpix/s | Notas
------|---------|--------|--------|-------
1     | 1       | 22,63  | 22,63  | baseline
2     | 2       | 20,09  | 20,09  | leve queda (overhead SMT)
3     | 3       | 26,66  | 26,66  | 2o nucleo fisico
4     | 4       | 28,36  | 28,36  |
5     | 5       | 37,17  | 37,17  | SALTO: 3o nucleo fisico
6     | 6       | 32,65  | 32,65  |
7     | 7       | 32,27  | 32,27  |
8     | 7       | 36,24  | 36,24  |
12    | 7       | 39,34  | 39,34  |
14    | 7       | 40,24  | 40,24  |
16    | 7       | 40,78  | 40,78  |
24    | 7       | 42,82  | 42,82  |
28    | 7       | 46,89  | 46,89  |
32    | 7       | 49,55  | 49,55  | melhor resultado desta seção
```

**Por que mais núcleos RV32I = mais FPS (ao contrário da análise anterior)?**

Na versão anterior, a análise mostrava que núcleos adicionais quase não afetavam
o FPS porque o gargalo era a limpeza O(N) serial (que não escalava). Com a
limpeza na Fase 2 paralela, o gargalo mudou para o fragment shader. Cada núcleo
RV32I processa `N/C` pixels por frame. Com mais núcleos, cada núcleo processa
menos pixels → executa menos instruções RV32I → a thread HW termina mais rápido.

Mecanismo concreto com 32 núcleos:
- Pixels por núcleo: 1.000.000 / 32 = 31.250 pixels
- Instruções de fragmento por thread HW: 31.250 × 18 = 562.500 instruções RV32I
- Com 1 núcleo: 1.000.000 × 18 = 18.000.000 instruções RV32I por thread

Ratio: 18M / 562K = 32× menos trabalho por thread → FPS sobe (limitado pela fase
serial, mas bem mais do que antes).

Speedup total de núcleos: 49,55 / 22,63 = **2,19×** (de 1 para 32 núcleos).

---

### Seção 3 — Impacto da Resolução (dados corrigidos)

Configuração: 14 núcleos, 7 threads.

```
Resolução     | Pixels      | FPS      | Mpix/s | Tamanho VRAM
--------------|-------------|----------|--------|-------------
128×128       | 16.384      | 2169,44  | 35,5   | ~0,1 MB  (cabe em L1)
256×256       | 65.536      | 591,14   | 38,7   | ~0,5 MB  (cabe em L2)
512×512       | 262.144     | 150,87   | 39,6   | ~2,0 MB  (cabe em L2)
640×480       | 307.200     | 131,90   | 40,5   | ~2,3 MB  (cabe em L2)
800×600       | 480.000     | 79,51    | 38,2   | ~3,7 MB  (quase L3)
1000×1000     | 1.000.000   | 38,77    | 38,8   | ~7,6 MB  (excede L3)
1280×720      | 921.600     | 41,11    | 37,9   | ~7,0 MB  (excede L3)
1920×1080     | 2.073.600   | 19,24    | 39,9   | ~15,8 MB (excede L3)
2560×1440     | 3.686.400   | 11,12    | 41,0   | ~28,1 MB (excede L3)
```

**Resultado mais importante: Mpix/s estável em ~38–41 em TODA a faixa.**

Isso significa que o sistema é **compute-bound** (limitado pela taxa de execução
de instruções RV32I, não pela largura de banda de memória). Se fosse
memory-bound, o Mpix/s cairia com resoluções maiores (mais pressão no cache L3).

O fato de que a VRAM em 1920×1080 (~16 MB) excede em 4× o cache L3 (4 MB) e
mesmo assim o Mpix/s permanece estável confirma: o gargalo é o emulador RV32I,
não o acesso à memória.

Diferença em relação à análise anterior (§31): naquela versão, o Mpix/s CRESCIA
com resolução porque a limpeza O(N) serial dominava o tempo de frame em
resoluções altas. Agora, com a limpeza paralela, o comportamento é
fundamentalmente diferente: puro compute-bound, Mpix/s constante.

---

### Lei de Amdahl — Dados Corrigidos

Com os dados novos, a fração serial s é estimada como:

```
S(4) = 36,49 / 21,39 = 1,706  →  s = (4/1,706 - 1) / (4 - 1) = 0,448
S(6) = 39,80 / 21,39 = 1,861  →  s = (6/1,861 - 1) / (6 - 1) = 0,445
Média: s ≈ 0,45
```

Verificação do ajuste para cada ponto:

```
n  | S_medido | S_Amdahl(s=0,45) | Erro   | Observação
---|----------|------------------|--------|---------------------
1  | 1,000    | 1,000            | 0%     | baseline
2  | 1,022    | 1,379            | -25,8% | SMT: sem paralelismo real
3  | 1,727    | 1,580            | +9,3%  | 2o nucleo fisico
4  | 1,706    | 1,702            | +0,2%  | ajuste excelente
5  | 1,462    | 1,786            | -18,1% | ruido de scheduler
6  | 1,861    | 1,846            | +0,8%  | ajuste excelente
7  | 1,755    | 1,887            | -7,0%  | inicio de saturacao HW
8  | 1,634    | 1,916            | -14,7% | saturacao HW dominante
```

O modelo de Amdahl funciona bem para n ∈ {4, 6} (erro < 1%). Os desvios:
- n=2: medido << previsto → SMT não adiciona paralelismo real
- n=5,7,8: medido < previsto → saturação de hardware threads (não modelada)

Teto teórico: `S_max = 1/0,45 = 2,22×` → ~47,5 FPS em 1000×1000

Resultado combinado (32 núcleos, 7 threads): 49,55 FPS = **2,32×** sobre o
baseline de 1 núcleo, **superando** o teto de Amdahl para thread-only porque a
adição de núcleos RV32I reduz o trabalho da fase paralela por thread HW,
um efeito não modelado pela Lei de Amdahl (que assume problema de tamanho fixo
por thread).

---

### Gráfico de Amdahl (fig4_amdahl.png)

O artigo IEEE inclui um gráfico com três curvas:

1. **Ideal linear** (linha pontilhada): S(n) = n — speedup teórico perfeito sem
   qualquer fração serial. Serve como limite superior de referência.

2. **Curva de Amdahl** (linha cheia): S(n) = 1/(0,45 + 0,55/n) — predição
   teórica para s = 0,45.

3. **Pontos medidos** (quadrados pretos): speedup real de cada configuração.

O gráfico evidencia três regiões:
- **n=2**: ponto bem abaixo da curva → efeito SMT (esperado, não modelado)
- **n=3,4,6**: pontos próximos da curva → modelo válido
- **n≥7**: pontos abaixo da curva → saturação de hardware (não modelada)

---

### Tabela de achados corrigida (comparação com §34)

```
Achado                              | Evidência corrigida          | Causa
------------------------------------|------------------------------|---------------------------
Pico em 6 threads (39,8 FPS)        | Seção 1 (§35)                | 6+2 sys = 8 HW slots exatos
FPS cresce com núcleos (até 32)     | Seção 2: 22,6→49,5 FPS      | Menos pixels/nucleo emulado
Mpix/s estável ~38-41               | Seção 3: 35-41 Mpix/s       | Compute-bound puro
s ≈ 0,45 (fração serial)            | Ajuste Amdahl n={4,6}        | Geometry interp. + swap serial
Teto de Amdahl: 47,5 FPS            | 1/0,45 × 21,39 FPS          | Lei de Amdahl
Superado com 32 núcleos: 49,5 FPS   | Seção 2                      | Menos trabalho/thread HW
```

---

### Artigo IEEE final — Estrutura

O artigo `PDF/main.tex` (compilado com tectonic, 5 páginas) contém:

| Seção | Conteúdo | Figuras/Tabelas |
|-------|----------|-----------------|
| I. Introdução | Contexto, motivação RISC-V, contribuições | — |
| II. Arquitetura | VRAM layout, SpinBarrier, pipeline 3 fases | Tab. I (VRAM) |
| III. Pipeline RV32I | Geometry shader, fragment shader | — |
| IV. Análise Experimental | Plataforma, threads, núcleos, resolução | Tab. II (plataforma), Tab. III (threads), Fig. 1, Tab. IV (resolução), Fig. 2, Fig. 3 |
| V. Discussão | Implicações emulação GPU em CPU, Amdahl, SpinBarrier | Fig. 4 (Amdahl), Tab. V (SpinBarrier) |
| VI. Trabalhos Futuros | 4 direções | — |
| VII. Conclusão | Síntese quantitativa | — |

**Referências usadas (4 de 6 originais, 2 removidas):**
- [1] Silva et al. 2024 — emulador RISC-V (motivação)
- [2] Pirassoli 2025 — RISC++ HLS (futuro trabalho)
- [3] Patterson & Hennessy 2014 — COD (SIMT paradigma)
- [4] Amdahl 1967 — Lei de Amdahl

**Removidas por pouca relevância no texto final:**
- Patterson & Waterman 2019 (guia RISC-V) — não citado no artigo final
- Herlihy & Shavit 2012 (Multiprocessor Programming) — a propriedade lock-free
  da VRAM é auto-evidente pelo layout disjunto, não requer citação

---

### Recomendação final de configuração

```
Hardware                  | Nucleos fisicos | COMPUTE_THREADS | NUM_CORES
--------------------------|-----------------|-----------------|----------
Laptop 2 cores / 4 HW    |       2         |       4         |    14
Desktop 4 cores / 8 HW   |       4         |       6         |    14-28
Desktop 6 cores / 12 HW  |       6         |       8         |    28-32
Desktop 8 cores / 16 HW  |       8         |      12         |    32
Workstation 16+ cores     |      16         |      20+        |    32+
```

**Regra revisada:** `COMPUTE_THREADS = núcleos_físicos + 2` (para saturar os
slots de HW sem sobrescrever). O aumento de `NUM_CORES` sempre melhora o FPS
marginalmente, pois divide o trabalho do fragment shader em pedaços menores
por thread HW.

---

*Documento do projeto GPU-V — Universidade Católica de Santos*
