# GPU-V — Emulador de GPU baseado em RISC-V RV32I

**Universidade Católica de Santos — Ciência da Computação**  
Milton Silva de Jesus · Joaquim Luis Malacarne Lima de Oliveira · Pedro de França Pereira

---

## Visão geral

GPU-V é um emulador de unidade de processamento gráfico implementado em C++17. Cada núcleo da GPU é modelado como uma CPU independente com a ISA RISC-V RV32I completa. O projeto simula em software o fluxo completo de uma GPU real:

```
shader (binário RV32I) → carregamento nos núcleos → execução paralela
→ leitura de máscara via MMIO → escrita na VRAM via MMIO
→ double buffering → exibição em tempo real (SDL2)
```

A demo atual renderiza um **cubo 3D rotacionando em tempo real**. A CPU calcula as transformações 3D e a rasterização de arestas; os 64 núcleos RV32I pintam cada pixel em paralelo consultando uma máscara de aresta gravada na VRAM.

---

## Estrutura de arquivos

```
GPU_WALTER/
├── RV32Asm.h        — mini-montador RV32I (namespace de funções inline)
├── VRAM.h           — memória de vídeo linear RGBA8888, lock-free
├── RV32ICore.h/.cpp — pipeline fetch/decode/execute de um núcleo RV32I
├── gpu.h/.cpp       — GPUManager: orquestração de núcleos e threads
├── prog.h/.cpp      — shader RV32I (build_cube_shader)
├── math3d.h/.cpp    — transformações 3D, projeção perspectiva, Bresenham
├── janela.h/.cpp    — componente gráfico SDL2
├── main.cpp         — loop principal de animação
└── Makefile         — sistema de build
```

---

## Dependências

| Dependência | Versão mínima | Uso |
|---|---|---|
| g++ | C++17 | Compilação |
| SDL2 | 2.0 | Janela gráfica em tempo real |
| pthreads | — | Threads dos núcleos |

### Instalação no Ubuntu/Debian

```bash
sudo apt install build-essential libsdl2-dev
```

---

## Compilação e execução

```bash
make        # compila
./gpu_v     # executa
make clean  # limpa objetos e binário
```

Saída esperada no terminal:

```
GPU-V | shader: 19 instruções | 128×128 | 64 núcleos | 256 px/núcleo
```

Feche a janela com **ESC** ou pelo botão de fechar.

---

## Arquitetura

### 1. Mini-montador RV32I (`RV32Asm.h`)

Namespace de funções C++ `inline` que convertem operandos em palavras de instrução de 32 bits no formato RISC-V. Não há arquivo de assembly — o programa binário é construído diretamente em C++.

```cpp
// Chamada C++:
LUI(8, 0x10)

// A função monta os campos bit a bit:
inline uint32_t LUI(int rd, int imm20) {
    return ((imm20 & 0xFFFFF) << 12) | (rd << 7) | 0x37;
}

// Resultado: 0x00010437  (palavra de 32 bits = instrução RISC-V pronta)
//
// Formato U-type:
//  bits [31:12] = imm20 = 0x10  →  registrador receberá 0x00010000
//  bits [11:7]  = rd    = 8
//  bits [6:0]   = opcode = 0x37 (LUI)
```

---

### 2. Shader RV32I (`prog.cpp`)

`build_cube_shader()` retorna um `vector<uint32_t>` — o binário completo do shader. O mesmo binário é carregado em todos os 64 núcleos. Cada núcleo sabe qual é o seu ID (`mhartid` em `a0`) e usa isso para calcular quais pixels são seus.

#### Layout da VRAM unificada (tamanho = 2 × total_pixels)

```
back_buffer.memory:
┌─────────────────────────────┬─────────────────────────────┐
│  [0 .. 16383]               │  [16384 .. 32767]           │
│  buffer de cor (saída)      │  máscara de aresta (entrada)│
│  núcleos escrevem aqui      │  CPU escreve antes do frame │
└─────────────────────────────┴─────────────────────────────┘
```

#### Algoritmo do shader (pseudocódigo)

```
a0  = mhartid                            ← preenchido pelo construtor RV32ICore

t0  = pixels_per_core (256)              ← LUI + ADDI
t1  = mhartid × 256 (= base_addr)       ← ADDI(shift=8) + SLL
t2  = total_pixels  (16384 = mask_base) ← LUI + ADDI
s0  = 0x0000FFFF (azul)                 ← LUI(0x10) + ADDI(-1)
t3  = 0 (i = 0)

loop:
    t4 = base_addr + i                   ← ADD   (output_addr)
    t5 = mask_base + output_addr         ← ADD   (mask_addr)
    t6 = VRAM[mask_addr]                 ← LW    (lê máscara via MMIO)
    se t6 == 0: VRAM[output_addr] = 0    ← BEQ + SW  (preta)
    senão:      VRAM[output_addr] = s0   ← SW        (azul)
    i++                                  ← ADDI
    se i != ppc: goto loop               ← BNE
ECALL
```

#### Mapa de registradores

| Reg | ABI | Valor |
|---|---|---|
| x10 | a0 | mhartid |
| x5  | t0 | pixels_per_core (256) |
| x6  | t1 | base_addr = mhartid × 256 |
| x7  | t2 | mask_base = 16384 |
| x8  | s0 | cor azul = 0x0000FFFF |
| x28 | t3 | i (contador do loop) |
| x29 | t4 | output_addr |
| x30 | t5 | mask_addr |
| x31 | t6 | valor lido da máscara |

---

### 3. Núcleos RV32I (`RV32ICore`)

Cada núcleo é uma instância autônoma com PC, banco de 32 registradores, memória de instrução local e referência à VRAM compartilhada.

#### Pipeline em `step()`

```
FETCH   → inst = instruction_memory[pc / 4]
DECODE  → opcode, rd, rs1, rs2, funct3, funct7, imediatos
EXECUTE → atualiza registradores / VRAM / PC
```

- `x0` é hardwired zero: toda escrita em `registers[0]` é revertida.
- `ECALL` retorna `false` de `step()`, encerrando o loop `execute()`.

#### ISA implementada

| Formato | Instruções |
|---|---|
| Tipo-R | ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND |
| Tipo-I (ALU) | ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI |
| Tipo-I (load) | LB, LH, LW, LBU, LHU — lê da VRAM via MMIO |
| Tipo-S | SW — escreve na VRAM via MMIO |
| Tipo-B | BEQ, BNE, BLT, BGE, BLTU, BGEU |
| Tipo-U | LUI, AUIPC |
| Tipo-J | JAL, JALR |
| Sistema | ECALL (encerra o núcleo) |

#### Barramento MMIO

Os núcleos não têm instruções especiais de vídeo. `LW` e `SW` em qualquer endereço são roteados para `vram_ref.read_pixel()` e `vram_ref.write_pixel()`. O shader lê a máscara e escreve cores usando as mesmas instruções de memória da ISA.

---

### 4. GPU Manager (`gpu.cpp`)

Orquestra núcleos, threads e buffers.

#### Threads

```
Thread de controle  →  chama dispatch_frame()
                        ├─ lança threads de núcleo em batches
                        ├─ aguarda cada batch (f.get())
                        └─ chama swap_buffers()

Thread de núcleo    →  executa run_core(RV32ICore*)
                        └─ chama core->execute() até ECALL
```

Batches são limitados a `std::thread::hardware_concurrency()` threads simultâneas — evita criar 64 threads em hardware com 8 cores físicos.

```
hw_threads = 8  →  64 núcleos / 8 = 8 batches

batch 0: núcleos  0–7   → 8 threads simultâneas → aguarda
batch 1: núcleos  8–15  → ...
...
batch 7: núcleos 56–63  → aguarda → swap
```

#### Double buffering

```
                  ┌──────────────────────────────────────┐
  CPU             │          back_buffer                 │
  set_mask() ────►│ [16384..32767] = máscara de aresta   │
                  │                                      │
  Núcleos 0–63   │ [0..16383]     = buffer de cor       │◄── run_core() escreve
                  └──────────────┬───────────────────────┘
                                 │ swap_buffers()
                                 │  copy [0..16383] → front_buffer
                                 │  fill [0..16383] → zeros
                                 ▼
                  ┌──────────────────────────────────────┐
  Janela vê ─────►│         front_buffer                 │
                  │  frame completo e consistente        │
                  └──────────────────────────────────────┘
```

---

### 5. Matemática 3D (`math3d.cpp`)

Executada inteiramente na CPU com `float`. Os núcleos não precisam de ponto flutuante.

| Função | O que faz |
|---|---|
| `rotate_yx` | Rotação em torno de Y e depois de X usando `cosf`/`sinf` |
| `project` | Projeção perspectiva: divide por `(3.5 + z)`, mapeia para tela |
| `bresenham` | Rasteriza segmentos de reta na máscara (layout row-major) |

---

### 6. Janela gráfica (`janela.cpp`)

Encapsula todo o SDL2. `main.cpp` não inclui `<SDL2/SDL.h>`.

| Método | Função |
|---|---|
| `Janela(title, w, h, scale)` | Cria janela, renderer e textura streaming |
| `poll_events()` | Processa eventos; retorna `false` se ESC ou fechar |
| `draw(pixels)` | Converte RRGGBBAA → ARGB8888 e renderiza |

---

## Fluxo completo por frame

```
main()
 │
 ├─ CPU: rotate_yx(vértices, angle)      → transforma os 8 vértices do cubo
 ├─ CPU: project(vértices)               → projeta para coordenadas de tela
 ├─ CPU: bresenham(mask, arestas)        → marca pixels de aresta na máscara
 │
 ├─ gpu.set_mask(mask)                   → grava máscara em back_buffer[16384..]
 ├─ gpu.load_program(shader)             → distribui 19 instruções para 64 núcleos
 ├─ gpu.dispatch_frame()
 │    ├─ 8 batches × 8 threads          → 64 núcleos executam shader em paralelo
 │    │    └─ cada núcleo lê máscara (LW) e pinta cor (SW) via MMIO
 │    └─ swap_buffers()                 → front_buffer recebe o frame pronto
 │
 ├─ janela.draw(gpu.get_pixels())        → exibe na tela
 │
 └─ angle += 0.02f  →  SDL_Delay(16ms)  → ~60 fps
```

---

## Como modificar

### Mudar resolução

Em `main.cpp`:

```cpp
const int    FB_W         = 128;   // largura em pixels
const int    FB_H         = 128;   // altura em pixels
const int    SCALE        = 5;     // escala da janela
const size_t TOTAL_PIXELS = FB_W * FB_H;
const size_t NUM_CORES    = 64;
```

> `TOTAL_PIXELS / NUM_CORES` precisa ser potência de 2 para o cálculo de `base_addr` via `SLL` funcionar.

### Mudar a cor do cubo

Em `prog.cpp`, troque o bloco que carrega `s0`. Exemplos:

```cpp
// Vermelho: 0xFF0000FF
prog.push_back(LUI (8, 0xFF001));   // 0xFF001000
prog.push_back(ADDI(8, 8, -0xFF1)); // ajuste fino

// Branco: 0xFFFFFFFF (LUI+ADDI encadeados)
prog.push_back(LUI (8, 0xFFFFF));
prog.push_back(ADDI(8, 8, -1));
```

O formato RGBA na VRAM é `0xRRGGBBAA` — R nos bits 31:24, G 23:16, B 15:8, A 7:0.

### Adicionar outro shader

1. Declare uma nova função em `prog.h`
2. Implemente em `prog.cpp` usando `RV32Asm`
3. Passe o vetor retornado para `gpu.load_program()` em `main.cpp`

---

## Limitações e trabalhos futuros

| Item | Status | Observação |
|---|---|---|
| ISA RV32I | Implementada | Todos os formatos R/I/S/B/U/J |
| Multiplicação (RV32M) | Não implementada | Shader usa SLL para pot. de 2 |
| Double buffering | Implementado | copy + fill entre back e front |
| Rasterização de sólidos | Não implementada | Apenas wireframe (arestas) |
| Síntese HLS (FPGA) | Trabalho futuro | Base C++ compatível com HLS |
| Extensão vetorial (RV32V) | Trabalho futuro | SIMD por núcleo |
