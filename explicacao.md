# GPU-V — Explicação Técnica Completa

> Este documento explica o projeto do zero, desde conceitos fundamentais até cada detalhe de implementação. Não é necessário conhecimento prévio de arquitetura de computadores ou gráficos 3D.

---

## Sumário

1. [O que é este projeto?](#1-o-que-é-este-projeto)
2. [O que é RISC-V?](#2-o-que-é-risc-v)
3. [O que é uma GPU e como ela funciona?](#3-o-que-é-uma-gpu-e-como-ela-funciona)
4. [Visão geral do pipeline](#4-visão-geral-do-pipeline)
5. [A VRAM e seu layout](#5-a-vram-e-seu-layout)
6. [Aritmética de ponto fixo Q8.8](#6-aritmética-de-ponto-fixo-q88)
7. [Matemática 3D: rotação e projeção](#7-matemática-3d-rotação-e-projeção)
8. [Rasterização: algoritmo de Bresenham](#8-rasterização-algoritmo-de-bresenham)
9. [O núcleo RV32I (RV32ICore)](#9-o-núcleo-rv32i-rv32icore)
10. [O mini-montador (RV32Asm.h)](#10-o-mini-montador-rv32asmh)
11. [O geometry shader em assembly](#11-o-geometry-shader-em-assembly)
12. [O fragment shader em assembly](#12-o-fragment-shader-em-assembly)
13. [O gerenciador de GPU (GPUManager)](#13-o-gerenciador-de-gpu-gpumanager)
14. [O loop principal (main.cpp)](#14-o-loop-principal-maincpp)
15. [Fluxo completo de um frame](#15-fluxo-completo-de-um-frame)

---

## 1. O que é este projeto?

O **GPU-V** é um emulador de GPU escrito em C++. Ele simula uma placa gráfica que possui 64 "shader cores" — cada um deles é um processador RISC-V RV32I completo rodando em sua própria thread.

**O objetivo principal:** mover todo o trabalho gráfico (transformação 3D, projeção perspectiva, desenho de arestas) para dentro dos núcleos simulados, assim como uma GPU real executa shaders em seus cores.

A CPU do computador real apenas:
1. Inicializa as tabelas de dados na memória da GPU
2. Atualiza o ângulo de rotação uma vez por frame
3. Dispara os shaders e aguarda o resultado

Todo o resto — senos, cossenos, multiplicações em ponto fixo, divisões para perspectiva, algoritmo de Bresenham — roda dentro dos 64 núcleos RV32I emulados.

---

## 2. O que é RISC-V?

**RISC-V** (lê-se "risco cinco") é uma **arquitetura de conjunto de instruções (ISA)** aberta e gratuita. ISA é a "linguagem" que um processador entende — define quais operações existem e como elas são codificadas em bits.

### Por que RISC?

**RISC** = *Reduced Instruction Set Computer*. A ideia é ter poucas instruções simples, cada uma fazendo uma coisa bem definida. Isso facilita a implementação do hardware.

**Contraste com CISC** (como x86): muitas instruções complexas, difíceis de emular.

### RV32I — a base

**RV32I** é o subconjunto base do RISC-V para processadores de 32 bits:

- **32 registradores** de 32 bits cada (`x0` a `x31`)
  - `x0` é sempre zero (hardwired)
  - Os demais são de uso geral
- **Instruções de 32 bits** com campos fixos
- Operações: aritméticas, lógicas, desvios, carga e armazenamento

### Extensão M

A extensão **M** adiciona multiplicação e divisão inteira:
- `MUL`, `MULH` — multiplicação (32 bits baixos / altos)
- `DIV`, `DIVU` — divisão com/sem sinal
- `REM`, `REMU` — resto da divisão

Neste projeto implementamos RV32I + M completos.

### Formato das instruções

Cada instrução tem 32 bits divididos em campos:

```
Bit:  31      25 24    20 19    15 14  12 11     7 6      0
      [ funct7 ][ rs2   ][ rs1   ][funct3][ rd    ][opcode]
                        Tipo-R (ex: ADD, SUB, MUL)

      [   imm[11:0]     ][ rs1   ][funct3][ rd    ][opcode]
                        Tipo-I (ex: ADDI, LW)
```

O **opcode** (7 bits) identifica a família de instrução. **funct3** e **funct7** distinguem instruções dentro da mesma família.

---

## 3. O que é uma GPU e como ela funciona?

Uma **GPU** (Graphics Processing Unit) é um processador massivamente paralelo especializado em gráficos. A ideia fundamental é:

> Em vez de um processador poderoso fazendo tudo sequencialmente, use **centenas de processadores simples** fazendo coisas similares ao mesmo tempo.

### Shaders

**Shaders** são programas que rodam dentro dos cores da GPU. No pipeline moderno existem dois tipos principais:

| Tipo | Entrada | Saída | Executa |
|------|---------|-------|---------|
| **Vertex Shader** | Coordenadas 3D | Posição na tela | Uma vez por vértice |
| **Fragment Shader** | Posição do pixel | Cor final | Uma vez por pixel |

Em nosso projeto:

| Nosso nome | Equivalente | O que faz |
|------------|-------------|-----------|
| **Geometry Shader** | Vertex Shader | Rotaciona, projeta, rasteriza arestas |
| **Fragment Shader** | Fragment Shader | Lê a máscara, decide cor do pixel |

### MIMD

Nossa GPU segue o modelo **MIMD** (Multiple Instruction, Multiple Data): cada núcleo pode executar instruções diferentes sobre dados diferentes. O geometry shader (núcleo 0) faz trabalho completamente diferente dos fragment shaders (núcleos 1–63).

---

## 4. Visão geral do pipeline

```
╔══════════════════════════════════════════════════════════════╗
║                    INICIALIZAÇÃO (1× só)                     ║
║  CPU calcula LUT sin/cos + posições do cubo → grava na VRAM  ║
╚══════════════════════════════════════════════════════════════╝
                            ↓ por frame
╔══════════════════════════════════════════════════════════════╗
║  CPU: grava angle_idx (0–255) na VRAM                        ║
╚══════════════════════════════════════════════════════════════╝
                            ↓
╔══════════════════════════════════════════════════════════════╗
║  GEOMETRY SHADER — Núcleo 0, sequencial                      ║
║  1. Lê angle_idx da VRAM                                     ║
║  2. Busca sin/cos nas LUTs                                   ║
║  3. Para cada um dos 8 vértices do cubo:                     ║
║     a. Aplica rotação Y (ângulo ay)                          ║
║     b. Aplica rotação X (ângulo ax = 0.6×ay)                 ║
║     c. Aplica projeção perspectiva → (sx, sy) em pixels      ║
║     d. Grava (sx, sy) na VRAM                                ║
║  4. Limpa edge mask na VRAM                                  ║
║  5. Para cada uma das 12 arestas do cubo:                    ║
║     Bresenham: risca pixels entre dois vértices → mask=1     ║
╚══════════════════════════════════════════════════════════════╝
                            ↓
╔══════════════════════════════════════════════════════════════╗
║  FRAGMENT SHADER — 64 núcleos, em paralelo                   ║
║  Cada núcleo processa 256 pixels (16384 ÷ 64):               ║
║    SE mask[pixel] == 1 → escreve AZUL                        ║
║    SENÃO              → escreve PRETO                        ║
╚══════════════════════════════════════════════════════════════╝
                            ↓
╔══════════════════════════════════════════════════════════════╗
║  GPU: swap de buffers (back → front)                         ║
║  SDL2: exibe o front_buffer na janela                        ║
╚══════════════════════════════════════════════════════════════╝
```

---

## 5. A VRAM e seu layout

**VRAM** (Video RAM) é a memória da GPU. Em nosso emulador é um simples `std::vector<uint32_t>` — um vetor de inteiros de 32 bits. Cada posição é acessada por **índice de palavra** (não byte).

```
N = 128 × 128 = 16.384 pixels

Índice       Conteúdo
──────────────────────────────────────────────────────────
[0,    N)    Color buffer — fragment shader escreve RGBA
[N,   2N)    Edge mask   — geometry shader escreve 0 ou 1
[2N + 0]     angle_idx   — CPU escreve (0–255) por frame
[2N + 1]     sin_lut[0]  ─╮
    ...                    ├ 256 entradas Q8.8
[2N + 256]   sin_lut[255] ─╯
[2N + 257]   cos_lut[0]  ─╮
    ...                    ├ 256 entradas Q8.8
[2N + 512]   cos_lut[255] ─╯
[2N + 513]   verts[0].x  ─╮
    ...                    ├ 8 vértices × 3 coords Q8.8
[2N + 536]   verts[7].z  ─╯
[2N + 537]   screen[0].x ─╮
    ...                    ├ 8 vértices × 2 coords (pixels)
[2N + 552]   screen[7].y ─╯
──────────────────────────────────────────────────────────
Total: 2N + 553 = 33.337 words
```

### Color buffer (pixel format)

Cada pixel no color buffer é um `uint32_t` no formato **RRGGBBAA**:
- Azul puro: `0x0000FFFF` (R=0, G=0, B=255, A=255)
- Preto: `0x00000000`

O SDL2 usa formato **ARGB8888**, então `Janela::draw()` converte antes de exibir.

### Por que o endereçamento é por palavra?

Em RISC-V real, loads e stores usam endereços de bytes. Neste emulador simplificamos: `LW rd, rs1, imm` lê `vram[rs1 + imm]` diretamente como índice de palavra. Isso simplifica o código assembly dos shaders (sem necessidade de multiplicar por 4 para navegar arrays).

---

## 6. Aritmética de ponto fixo Q8.8

Processadores inteiros não entendem números com vírgula como `0.707`. Para fazer matemática fracionária sem FPU (Floating Point Unit), usamos **ponto fixo**.

### A ideia

Escolhemos uma **escala** — neste projeto **256 (= 2⁸)** — e representamos qualquer número real multiplicando por essa escala e arredondando para inteiro:

```
número real → inteiro armazenado
────────────────────────────────
1.0         → 256
0.5         → 128
-1.0        → -256
0.707       → 181   (≈ sin 45°)
```

O formato se chama **Q8.8** porque temos 8 bits para a parte inteira e 8 bits para a parte fracionária (dentro de um int32 de 32 bits).

### Multiplicação em Q8.8

Se `A` e `B` são valores Q8.8:
```
A_real = A / 256
B_real = B / 256
A_real × B_real = (A × B) / 65536
```

Para recuperar o resultado em Q8.8:
```
resultado_Q88 = (A × B) / 256  =  (A × B) >> 8
```

**Exemplo:** `cos(0°) × sin(45°)` em Q8.8:
```
cos_Q88 = 256   (1.0 × 256)
sin_Q88 = 181   (0.707 × 256)
produto = 256 × 181 = 46336
resultado_Q88 = 46336 >> 8 = 181   ✓ (ainda representa 0.707)
```

Nenhum dos produtos intermédios ultrapassa 32 bits: `max = 256 × 256 = 65536`, dentro do limite do `int32`.

### Por que escala 256 e não 65536?

Com escala 256 (Q8.8):
- Produto de dois Q8.8: max = 256 × 256 = 65.536 → cabe em int32 ✓
- Não precisamos de `MULH` (alto 32 bits da multiplicação 64 bits) ✓

Com escala 65536 (Q16.16):
- Produto: max = 65536 × 65536 = 2^32 → **overflow** em int32 ✗
- Precisaria de `MULH` para capturar os 64 bits completos ✗

---

## 7. Matemática 3D: rotação e projeção

### Representação do cubo

O cubo tem 8 vértices nas posições `(±1, ±1, ±1)`. Em Q8.8 com escala 256:

```
Vértice    Real           Q8.8
v0      (-1, -1, -1)  (-256, -256, -256)
v1      (+1, -1, -1)  (+256, -256, -256)
...
v7      (-1, +1, +1)  (-256, +256, +256)
```

### Rotação em 3D

Para rotacionar o cubo, aplicamos duas rotações em sequência:

**Rotação em torno do eixo Y** (ângulo `ay`):
```
vx' =  vx × cos(ay) + vz × sin(ay)
vz' = -vx × sin(ay) + vz × cos(ay)
vy' = vy  (eixo Y não muda com rotação Y)
```

**Rotação em torno do eixo X** (ângulo `ax = 0.6 × ay`):
```
vy'' =  vy' × cos(ax) - vz' × sin(ax)
vz'' =  vy' × sin(ax) + vz' × cos(ax)
vx'' = vx'  (eixo X não muda com rotação X)
```

**Em ponto fixo Q8.8:**
```
vx'' = (vx × cos_y + vz × sin_y) >> 8
vz_tmp = (-vx × sin_y + vz × cos_y) >> 8
vy'' = (vy × cos_x - vz_tmp × sin_x) >> 8
vz'' = (vy × sin_x + vz_tmp × cos_x) >> 8
```

### Tabela LUT para sin/cos

O ângulo de rotação vai de 0 a 2π. Dividimos isso em **256 passos** (um passo ≈ 1.4°). Para cada passo `i`, pré-calculamos:

```
sin_lut[i] = round(sin(2π × i / 256) × 256)
cos_lut[i] = round(cos(2π × i / 256) × 256)
```

Esses valores são escritos na VRAM na inicialização. Durante a animação, a CPU só incrementa `angle_idx` de 0 a 255 em loop. O geometry shader lê `sin_lut[angle_idx]` e `cos_lut[angle_idx]` diretamente da VRAM.

Para a rotação X, o ângulo é `ax_idx = (angle_idx × 154) >> 8 ≈ angle_idx × 0.6`.

### Projeção perspectiva

Projeção perspectiva simula a percepção de profundidade: objetos mais distantes aparecem menores.

A fórmula usada é:
```
d = 3.5 / (3.5 + vz)
sx = (vx × d × 0.45 + 0.5) × W
sy = (-vy × d × 0.45 + 0.5) × H
```

Onde `d` é o **fator de perspectiva** (quanto menor o z, maior o d, maior o objeto).

**Em ponto fixo Q8.8:**

O denominador `3.5 + vz` em Q8.8 é `896 + vz_fp` (onde `896 = 3.5 × 256`).

O numerador do fator `d` em Q8.8 precisa ser `3.5 × 256²` para compensar ambos os escalamentos:
```
numerador = 3.5 × 256 × 256 = 229376
d_fp = 229376 / (896 + vz_fp)     ← usa a instrução DIV
```

Para as coordenadas de tela em pixels:
```
sx = ((vx_fp × d_fp) >> 8) × 58 >> 8 + 64
```
Onde:
- `× d_fp >> 8` aplica perspectiva mantendo Q8.8
- `× 58 >> 8` aplica a escala `0.45 × 128 = 57.6 ≈ 58` e converte de Q8.8 para pixels
- `+ 64` centraliza (= W/2)

---

## 8. Rasterização: algoritmo de Bresenham

Dado dois pontos `(x0,y0)` e `(x1,y1)` na tela, precisamos descobrir quais pixels ficam entre eles e marcá-los na edge mask.

O **algoritmo de Bresenham** faz isso usando apenas adição e subtração inteiras (sem divisão ou ponto flutuante), o que o torna ideal para rodar em RISC-V.

### A ideia intuitiva

Imagine caminhar de `(x0,y0)` até `(x1,y1)`:
- A cada passo, avançamos pelo eixo com maior variação (dx ou dy)
- Usamos um "acumulador de erro" para decidir quando dar um passo no eixo menor

### Pseudocódigo

```
dx = |x1 - x0|
dy = |y1 - y0|
step_x = x0 < x1 ? +1 : -1
step_y = y0 < y1 ? +1 : -1
err = dx - dy

loop:
    se 0 ≤ x0 < W e 0 ≤ y0 < H:
        mask[y0 × W + x0] = 1          ← marca o pixel
    se x0 == x1 e y0 == y1: fim
    e2 = 2 × err
    se e2 > -dy: err -= dy;  x0 += step_x
    se e2 <  dx: err += dx;  y0 += step_y
```

### Clipping automático

A verificação `0 ≤ x < 128 e 0 ≤ y < 128` descarta pixels fora da tela sem precisar de um passo de clipping separado. Usamos o truque de **comparação sem sinal (SLTIU)**:

```assembly
SLTIU x30, x26, 128    # x30 = 1 se (uint32)x26 < 128
```

Números negativos em `int32` têm valor enorme quando interpretados como `uint32`, então `SLTIU` descarta automaticamente tanto `x < 0` quanto `x ≥ 128` com uma única instrução.

---

## 9. O núcleo RV32I (RV32ICore)

### Visão geral

```cpp
class RV32ICore {
    uint32_t pc;            // Program Counter
    uint32_t registers[32]; // 32 registradores
    uint32_t mhartid;       // ID único do núcleo (hart ID)
    VRAM& vram_ref;         // referência à VRAM compartilhada
    std::vector<uint32_t> instruction_memory; // programa carregado
};
```

### Ciclo fetch-decode-execute

Cada chamada a `step()` executa um ciclo:

```
FETCH:   inst = instruction_memory[pc / 4]
DECODE:  extrai opcode, rd, rs1, rs2, funct3, funct7 dos bits
EXECUTE: switch(opcode) → realiza a operação
UPDATE:  pc = next_pc (ou branch target se desvio foi tomado)
```

### Extração de campos

Todos os formatos de instrução RISC-V têm os mesmos campos nos mesmos bits, o que simplifica muito o decode:

```cpp
uint32_t opcode = inst & 0x7F;           // bits [6:0]
uint32_t rd     = (inst >> 7)  & 0x1F;  // bits [11:7]
uint32_t funct3 = (inst >> 12) & 0x07;  // bits [14:12]
uint32_t rs1    = (inst >> 15) & 0x1F;  // bits [19:15]
uint32_t rs2    = (inst >> 20) & 0x1F;  // bits [24:20]
uint32_t funct7 = (inst >> 25) & 0x7F;  // bits [31:25]
```

### Identificação das instruções (tipo-R)

Para instruções do tipo-R (ADD, MUL, etc.), a chave do switch usa `(funct7 << 3) | funct3`:

| Instrução | funct7 | funct3 | Chave   |
|-----------|--------|--------|---------|
| ADD       | 0x00   | 0      | `0x000` |
| SUB       | 0x20   | 0      | `0x100` |
| SLL       | 0x00   | 1      | `0x001` |
| SRA       | 0x20   | 5      | `0x105` |
| MUL       | 0x01   | 0      | `0x008` |
| MULH      | 0x01   | 1      | `0x009` |
| DIV       | 0x01   | 4      | `0x00C` |
| REM       | 0x01   | 6      | `0x00E` |

### Acesso à VRAM

O emulador simplifica o barramento de memória: `LW` e `SW` acessam diretamente a VRAM por índice de palavra:

```cpp
// LW: carrega da VRAM
uint32_t addr = registers[rs1] + imm;
registers[rd] = vram_ref.read_pixel(addr);  // = vram.memory[addr]

// SW: armazena na VRAM
uint32_t addr = registers[rs1] + imm;
vram_ref.write_pixel(addr, registers[rs2]); // vram.memory[addr] = valor
```

### x0 é sempre zero

Ao final de cada `step()`:
```cpp
registers[0] = 0;  // x0 hardwired para zero
```

Isso garante que instruções como `ADDI x5, x0, 42` (carregar constante) funcionem corretamente, mesmo que alguma instrução tente escrever em x0.

---

## 10. O mini-montador (RV32Asm.h)

Em vez de escrever os opcodes em hexadecimal na mão, usamos funções inline C++ que montam as palavras de instrução:

```cpp
namespace RV32Asm {
    // ADD rd, rs1, rs2
    inline uint32_t ADD(int rd, int rs1, int rs2) {
        return (rs2 << 20) | (rs1 << 15) | (0x0 << 12) | (rd << 7) | 0x33;
    }
    //  bits [31:25]=0  [24:20]=rs2  [19:15]=rs1  [14:12]=0  [11:7]=rd  [6:0]=0x33
}
```

Para **branches**, o imediato tem encoding especial (bits embaralhados para simplificar hardware):

```cpp
inline uint32_t _branch(int rs1, int rs2, int f3, int o) {
    return (((o>>12)&1)<<31) | (((o>>5)&0x3F)<<25) |
           (rs2<<20)         | (rs1<<15)            |
           (f3<<12)          | (((o>>1)&0xF)<<8)    |
           (((o>>11)&1)<<7)  | 0x63;
}
```

O offset `o` é em bytes, relativo ao PC da instrução de branch.

---

## 11. O geometry shader em assembly

Este é o programa mais complexo. Tem **540 instruções** e roda apenas no núcleo 0.

### Mapa de registradores

```
x4  (tp)  = N (total de pixels = 16384)
x5  (t0)  = sin_base  = endereço da LUT de seno na VRAM
x6  (t1)  = cos_base  = endereço da LUT de cosseno
x7  (t2)  = verts_base = coordenadas dos vértices na VRAM
x8  (s0)  = screen_base = coordenadas de tela na VRAM
x11 (a1)  = sin_y (sin do ângulo Y atual)
x12 (a2)  = cos_y
x13 (a3)  = sin_x
x14 (a4)  = cos_x
x15 (a5)  = vx   (componente x do vértice em processamento)
x16 (a6)  = vy
x17 (a7)  = vz
x28 (t3)  = vx' (depois da rotação Y)
x29 (t4)  = vy' (depois da rotação X)
x30 (t5)  = temporário / vz_tmp
x31 (t6)  = temporário / vz'
x21 (s5)  = sx (coordenada de tela x)
x22 (s6)  = sy
x24 (s8)  = contador do loop de vértices (0–7)
x26 (s10) = Bresenham: x corrente
x27 (s11) = Bresenham: y corrente
x19 (s3)  = Bresenham: dx
x20 (s4)  = Bresenham: dy
x21 (s5)  = Bresenham: step_x (±1) [reusa sx, mas em fase diferente]
x22 (s6)  = Bresenham: step_y (±1)
x23 (s7)  = Bresenham: err
```

### Seção 1: Prólogo (19 instruções)

```assembly
# Carrega N em x4
LUI  x4, 4          # x4 = 4 << 12 = 16384

# Deriva endereços base da VRAM
ADD  x5, x4, x4    # x5 = 2N
ADDI x5, x5, 1     # x5 = 2N+1 (sin_base)
ADDI x6, x5, 256   # x6 = 2N+257 (cos_base)
ADDI x7, x6, 256   # x7 = 2N+513 (verts_base)
ADDI x8, x7, 24    # x8 = 2N+537 (screen_base)

# Lê angle_idx da VRAM no endereço 2N
LW   x10, x5, -1   # x10 = VRAM[x5-1] = VRAM[2N] = angle_idx

# Calcula ax_idx ≈ angle_idx × 0.6
ADDI x28, x0, 154
MUL  x28, x10, x28
SRAI x28, x28, 8   # x28 = ax_idx

# Carrega sin/cos para ambas as rotações
ADD  x29, x5, x10  # endereço = sin_base + angle_idx
LW   x11, x29, 0   # x11 = sin_y
ADD  x29, x6, x10
LW   x12, x29, 0   # x12 = cos_y
ADD  x29, x5, x28
LW   x13, x29, 0   # x13 = sin_x
ADD  x29, x6, x28
LW   x14, x29, 0   # x14 = cos_x
```

### Seção 2: Loop de vértices (46 instruções por iteração × 8 = executa 8 vezes)

```assembly
ADDI x24, x0, 0    # v = 0

vertex_loop:
    # Calcula endereço do vértice v na VRAM: verts_base + v×3
    ADDI x10, x0, 3
    MUL  x10, x24, x10  # v×3
    ADD  x10, x10, x7   # verts_base + v×3

    # Carrega (vx, vy, vz)
    LW  x15, x10, 0   # vx
    LW  x16, x10, 1   # vy
    LW  x17, x10, 2   # vz

    # Rotação Y: vx' = (vx×cos_y + vz×sin_y) >> 8
    MUL  x28, x15, x12   # vx × cos_y
    MUL  x29, x17, x11   # vz × sin_y
    ADD  x28, x28, x29
    SRAI x28, x28, 8     # x28 = vx'

    # Rotação Y: vz_tmp = (vz×cos_y - vx×sin_y) >> 8
    MUL  x29, x15, x11   # vx × sin_y
    MUL  x30, x17, x12   # vz × cos_y
    SUB  x30, x30, x29
    SRAI x30, x30, 8     # x30 = vz_tmp

    # Rotação X: vy' = (vy×cos_x - vz_tmp×sin_x) >> 8
    MUL  x29, x16, x14
    MUL  x31, x30, x13
    SUB  x29, x29, x31
    SRAI x29, x29, 8     # x29 = vy'

    # Rotação X: vz' = (vy×sin_x + vz_tmp×cos_x) >> 8
    MUL  x31, x16, x13
    MUL  x10, x30, x14
    ADD  x31, x31, x10
    SRAI x31, x31, 8     # x31 = vz'

    # Perspectiva: d = 229376 / (896 + vz')
    LUI  x10, 56          # x10 = 229376 (= 56 × 4096)
    ADDI x31, x31, 896    # x31 = denominador
    DIV  x10, x10, x31    # x10 = d (Q8.8)

    # sx = ((vx'×d)>>8) × 58 >> 8 + 64
    MUL  x30, x28, x10
    SRAI x30, x30, 8
    ADDI x31, x0, 58
    MUL  x30, x30, x31
    SRAI x30, x30, 8
    ADDI x21, x30, 64    # x21 = sx

    # sy = (-(vy'×d)>>8) × 58 >> 8 + 64
    MUL  x30, x29, x10
    SRAI x30, x30, 8
    SUB  x30, x0, x30    # nega (y invertido)
    ADDI x31, x0, 58
    MUL  x30, x30, x31
    SRAI x30, x30, 8
    ADDI x22, x30, 64    # x22 = sy

    # Escreve (sx, sy) na VRAM
    ADDI x31, x0, 2
    MUL  x31, x24, x31   # v×2
    ADD  x31, x31, x8    # screen_base + v×2
    SW   x21, x31, 0     # screen_verts[v×2] = sx
    SW   x22, x31, 1     # screen_verts[v×2+1] = sy

    # Incrementa e verifica fim do loop
    ADDI x24, x24, 1
    ADDI x10, x0, 8
    BNE  x24, x10, -180  # volta ao início do loop (45 instr × 4 bytes = 180)
```

### Seção 3: Limpa a edge mask (5 instruções)

```assembly
ADDI x10, x4, 0    # x10 = N (início da mask)
ADD  x30, x4, x4   # x30 = 2N (fim)

clear_loop:
    SW   x0, x10, 0    # VRAM[x10] = 0
    ADDI x10, x10, 1   # avança
    BNE  x10, x30, -8  # volta se não chegou ao fim (2 instrs atrás)
```

### Seção 4: 12 blocos Bresenham (39 instruções cada)

Para cada aresta `(v0, v1)`, emite um bloco autossuficiente:

```assembly
# Setup (19 instruções):
LW   x26, x8, v0×2       # x26 = sx[v0]
LW   x27, x8, v0×2+1     # x27 = sy[v0]
LW   x17, x8, v1×2       # x17 = sx[v1]
LW   x18, x8, v1×2+1     # x18 = sy[v1]

# |dx| via truque do bit de sinal:
SUB  x19, x17, x26        # x1 - x0
SRAI x30, x19, 31         # máscara de sinal: 0x00000000 ou 0xFFFFFFFF
XOR  x19, x19, x30        # flip bits se negativo
SUB  x19, x19, x30        # +1 se negativo → x19 = |dx|

# Mesmo para |dy|
SUB  x20, x18, x27
SRAI x30, x20, 31
XOR  x20, x20, x30
SUB  x20, x20, x30        # x20 = |dy|

# step_x = (x0<x1) ? +1 : -1  via SLT + SLLI + ADDI:
SLT  x21, x26, x17        # x21 = 1 se x0<x1, else 0
SLLI x21, x21, 1          # x21 = 2 se x0<x1, else 0
ADDI x21, x21, -1         # x21 = +1 se x0<x1, else -1

# step_y (mesmo padrão)
SLT  x22, x27, x18
SLLI x22, x22, 1
ADDI x22, x22, -1         # x22 = step_y

SUB  x23, x19, x20        # err = dx - dy

# Loop Bresenham (20 instruções, offsets relativos ao início do loop):
# +0:  SLTIU x30, x26, 128   → x30=1 se x ∈ [0,127]
# +4:  BEQ   x30, x0, +32   → se fora, pula escrita
# +8:  SLTIU x30, x27, 128   → verifica y
# +12: BEQ   x30, x0, +24   → se fora, pula escrita
# +16: SLLI  x30, x27, 7    → y × 128
# +20: ADD   x30, x30, x26  → y×128 + x
# +24: ADD   x30, x30, x4   → + N (base da mask)
# +28: ADDI  x31, x0, 1
# +32: SW    x31, x30, 0    → mask[y×128+x] = 1
# +36: BNE   x26, x17, +8  → se x0≠x1, vai para not_done
# +40: BEQ   x27, x18, +40 → se y0=y1 também, termina
# +44: SLLI  x30, x23, 1   → e2 = 2×err  (not_done)
# +48: ADD   x31, x30, x20 → e2 + dy
# +52: BGE   x0, x31, +12  → se e2+dy≤0, pula passo X
# +56: SUB   x23, x23, x20 → err -= dy
# +60: ADD   x26, x26, x21 → x0 += step_x
# +64: BGE   x30, x19, +12 → se e2≥dx, pula passo Y
# +68: ADD   x23, x23, x19 → err += dx
# +72: ADD   x27, x27, x22 → y0 += step_y
# +76: BEQ   x0, x0, -76  → volta ao início do loop
# +80: (próximo bloco / ECALL)
ECALL                       # termina o núcleo
```

---

## 12. O fragment shader em assembly

Muito mais simples — **19 instruções** totais, rodam em todos os 64 núcleos simultaneamente.

```assembly
# Prologue: calcula região de pixels deste núcleo
LUI  t0, ppc_hi           # t0 = pixels_per_core (ppc = 256)
ADDI t0, t0, ppc_lo
ADDI t5, x0, shift        # shift = log2(ppc) = 8
SLL  t1, a0, t5           # t1 = base_addr = mhartid × ppc

LUI  t2, N_hi             # t2 = N (mask_base)
ADDI t2, t2, N_lo

LUI  s0, 0x10             # s0 = 0x00010000
ADDI s0, s0, -1           # s0 = 0x0000FFFF (RGBA azul)

ADDI t3, x0, 0            # i = 0

# Loop (9 instruções, 36 bytes):
loop:
    ADD  t4, t1, t3       # output_addr = base_addr + i
    ADD  t5, t2, t4       # mask_addr = N + output_addr
    LW   t6, t5, 0        # t6 = mask[output_addr]
    BEQ  t6, x0, +12      # se mask=0, escreve preto (+12 bytes → SW preto)
    SW   s0, t4, 0        # escreve AZUL
    BEQ  x0, x0, +8       # pula sobre SW preto (+8 bytes)
    SW   x0, t4, 0        # escreve PRETO
    ADDI t3, t3, 1        # i++
    BNE  t3, t0, -32      # se i ≠ ppc, volta (8 instrs × 4 = 32)

ECALL
```

Cada núcleo `k` processa os pixels do índice `k×256` até `k×256+255`.

---

## 13. O gerenciador de GPU (GPUManager)

### Double buffering

A GPU mantém dois buffers:

```
back_buffer  → onde os shaders escrevem (tamanho total da VRAM: 2N+553)
front_buffer → o frame exibido ao usuário (tamanho: N pixels)
```

Quando o fragment shader termina, `swap_buffers()` copia o color buffer do back para o front e zera o back para o próximo frame.

### Thread pool

O `dispatch_frame()` divide os 64 núcleos em **batches** de tamanho `NUM_THREADS` (ou `hardware_concurrency()` se for 0). Cada núcleo roda em sua própria thread:

```cpp
for (batch em batches de hw threads) {
    for (cada core no batch)
        futures.push_back(async(launch::async, run_core, &core[i]));
    for (cada future)
        f.get();  // aguarda o batch terminar
}
```

### dispatch_geometry vs dispatch_frame

```cpp
// Geometry: executa apenas o núcleo 0, síncrono, sem thread extra
void dispatch_geometry(size_t core_id = 0) {
    processing_units[core_id].execute();
}

// Fragment: todos os núcleos em paralelo
void dispatch_frame() {
    // batches de NUM_THREADS threads
    // ... swap_buffers() ao final
}
```

---

## 14. O loop principal (main.cpp)

```cpp
int main() {
    // 1. Inicialização única
    GPUManager gpu(NUM_CORES, N, NUM_THREADS);
    init_geometry_vram(gpu.vram(), N);          // grava LUTs e vértices
    auto geom_prog = build_geometry_shader(...); // compila shader
    auto frag_prog = build_fragment_shader(...);

    uint32_t angle_idx = 0;

    while (janela.poll_events()) {
        // 2. Atualização mínima da CPU (1 write na VRAM)
        gpu.set_angle(angle_idx);

        // 3. Geometry shader: roda no núcleo 0
        gpu.load_geometry(geom_prog, 0);
        gpu.dispatch_geometry(0);

        // 4. Fragment shader: roda nos 64 núcleos
        gpu.load_program(frag_prog);
        gpu.dispatch_frame();

        // 5. Exibe
        janela.draw(gpu.get_pixels());

        angle_idx = (angle_idx + 1) & 0xFF;   // 256 passos = 1 volta
        sleep_for(16ms);
    }
}
```

---

## 15. Fluxo completo de um frame

Vamos traçar o caminho de um único pixel — por exemplo, o pixel `(64, 18)` que pertence à aresta superior da face frontal do cubo.

### Passo 1: CPU grava angle_idx

```
VRAM[2N + 0] = 42   (ângulo ≈ 59° no eixo Y)
```

### Passo 2: Geometry shader carrega sin/cos

```
angle_idx = 42
sin_y = VRAM[sin_base + 42] = round(sin(2π×42/256) × 256) = 165  (≈0.644)
cos_y = VRAM[cos_base + 42] = round(cos(2π×42/256) × 256) = 196  (≈0.766)
ax_idx = (42 × 154) >> 8 = 25
sin_x = VRAM[sin_base + 25] = 154  (≈0.602)
cos_x = VRAM[cos_base + 25] = 206  (≈0.805)
```

### Passo 3: Rotaciona vértice v5 (+1, -1, +1)

```
vx=256, vy=-256, vz=256 (Q8.8)

Rot Y:
  vx' = (256×196 + 256×165) >> 8 = (50176 + 42240) >> 8 = 92416 >> 8 = 361
  vz_tmp = (256×196 - 256×165) >> 8 = 8192 >> 8 = 32

Rot X:
  vy' = (-256×206 - 32×154) >> 8 = (-52736 - 4928) >> 8 = -57664 >> 8 = -225
  vz' = (-256×154 + 32×206) >> 8 = (-39424 + 6592) >> 8 = -32832 >> 8 = -128
```

### Passo 4: Projeção perspectiva

```
d = 229376 / (896 + (-128)) = 229376 / 768 = 298  (Q8.8 ≈ 1.164)

sx = ((361 × 298) >> 8) × 58 >> 8 + 64
   = (107578 >> 8) × 58 >> 8 + 64
   = 420 × 58 >> 8 + 64
   = 24360 >> 8 + 64
   = 95 + 64 = 159

sy = (-((-225) × 298) >> 8) × 58 >> 8 + 64
   = (67050 >> 8) × 58 >> 8 + 64
   = 261 × 58 >> 8 + 64
   = 15138 >> 8 + 64
   = 59 + 64 = 123  (dentro do framebuffer 128×128)
```

### Passo 5: Bresenham rasteriza a aresta v4→v5

Suponha `v4=(18,109)`, `v5=(159,123)`. O algoritmo marca pixels ao longo da reta, incluindo o pixel `(64, 113)` por exemplo.

```
Para pixel (x=64, y=113):
  SLTIU 128 → 64 < 128 ✓, 113 < 128 ✓
  addr = N + y×128 + x = 16384 + 113×128 + 64 = 16384 + 14464 + 64 = 30912
  VRAM[30912] = 1   ← pixel marcado na edge mask
```

### Passo 6: Fragment shader — núcleo 24

O pixel 30912 (- N = 14528) está na fatia do núcleo 24 + alguma posição local.

```
Núcleo 24:
  base_addr = 24 × 256 = 6144   (pixel 6144 a 6399 do color buffer)
  mask_addr = N + base_addr = 16384 + 6144 = 22528
```

(Pixel 14528 pertence ao núcleo `14528 ÷ 256 = 56`. O núcleo 56 processa:)

```
Núcleo 56:
  base_addr = 56 × 256 = 14336
  Para i = 14528 - 14336 = 192:
    output_addr = 14528
    mask_addr = N + 14528 = 30912
    t6 = VRAM[30912] = 1       ← mask está setada!
    → escreve AZUL em VRAM[14528] = 0x0000FFFF
```

### Passo 7: Swap e exibição

O color buffer (com o pixel azul em posição 14528) é copiado para o front_buffer.
O SDL2 converte `0x0000FFFF` (RRGGBBAA) para `0xFF0000FF` (ARGB) e exibe o pixel azul na janela.

---

## Resumo das dependências entre módulos

```
config.h ←── todos os módulos
vram_layout.h ←── geom_shader, gpu, main
RV32Asm.h ←── geom_shader, frag_shader
VRAM.h ←── RV32ICore, gpu
RV32ICore ←── gpu
geom_shader ←── main
frag_shader ←── main
gpu ←── main
janela ←── main
```

---

*Documento gerado para o projeto GPU-V — Universidade Católica de Santos*
