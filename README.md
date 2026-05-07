# GPU-V — Emulador de GPU baseado em RISC-V RV32I

**Universidade Católica de Santos — Ciência da Computação**  
Milton Silva de Jesus · Joaquim Luis Malacarne Lima de Oliveira · Pedro de França Pereira

---

## Visão geral

GPU-V é um emulador de unidade de processamento gráfico implementado em C++17. Cada núcleo da GPU é modelado como uma CPU independente com a ISA RISC-V RV32I completa. O projeto simula em software o fluxo completo de uma GPU real: shader assembly → carregamento nos núcleos → execução paralela → gravação na VRAM via MMIO → double buffering → exibição visual.

---

## Estrutura de arquivos

```
GPU_WALTER/
├── main.cpp         — GPUManager, shader assembler, visualizador SDL2, main()
├── RV32ICore.h      — Declaração do núcleo RV32I
├── RV32ICore.cpp    — Implementação completa do pipeline (fetch/decode/execute)
├── VRAM.h           — Memória de vídeo linear RGBA8888, lock-free
└── gpu_v            — Binário compilado
```

---

## Dependências

| Dependência | Versão mínima | Uso |
|---|---|---|
| g++ / clang++ | C++17 | Compilação |
| SDL2 | 2.0 | Janela visual antes/depois |

### Instalação no Ubuntu/Debian

```bash
sudo apt install build-essential libsdl2-dev
```

---

## Compilação

```bash
g++ -std=c++17 -O2 -o gpu_v main.cpp RV32ICore.cpp $(pkg-config --cflags --libs sdl2)
```

---

## Execução

```bash
./gpu_v
```

Saída esperada no terminal:

```
=== GPU-V: Emulador RISC-V RV32I ===
Núcleos      : 64
Pixels total : 1024
Pixels/núcleo: 16

Shader compilado: 22 instrução(ões).
[GPU Manager] Disparando 64 núcleos em 8 thread(s) físicas.
[GPU Manager] Back Buffer pronto.
[GPU Manager] Swap efetuado — Front Buffer exibido.

=== Diagnóstico de Frame ===
  Pixels totais : 1024
  Pixels escritos (≠0): 1024
  ...

[Visualizer] Janela aberta — faixa VERMELHA = ANTES | faixa VERDE = DEPOIS
             Pressione ESC ou feche a janela para encerrar.
```

Após o terminal, abre uma janela SDL2. Feche com **ESC** ou pelo botão de fechar.

---

## Arquitetura

O projeto é dividido em quatro componentes que refletem o design de uma GPU real.

### 1. GPU Manager (`GPUManager` em `main.cpp`)

Orquestra toda a simulação. Suas responsabilidades:

- Instanciar os N núcleos RV32I
- Distribuir o mesmo programa shader para todos os núcleos via `load_program()`
- Gerenciar um **thread pool dinâmico** limitado ao número de threads físicas do hardware (`std::thread::hardware_concurrency()`), evitando sobrecarga do SO
- Dividir os núcleos em batches e executá-los com `std::async(std::launch::async, ...)`
- Aguardar todos os núcleos finalizarem (`future::get()`) antes do swap
- Realizar o **double buffering** real entre back e front buffer

```
┌─────────────────────────────────────────────────┐
│                  GPU Manager                    │
│                                                 │
│  load_program() → distribui para 64 núcleos     │
│  dispatch_frame():                              │
│    batch 0: núcleos 0–7   → 8 threads físicas   │
│    batch 1: núcleos 8–15  → 8 threads físicas   │
│    ...                                          │
│    batch 7: núcleos 56–63 → 8 threads físicas   │
│  swap_buffers() → back ↔ front, back limpo      │
└─────────────────────────────────────────────────┘
```

### 2. Núcleos de processamento RV32I (`RV32ICore`)

Cada núcleo é uma instância autônoma com:

| Campo | Tipo | Descrição |
|---|---|---|
| `pc` | `uint32_t` | Contador de programa exclusivo |
| `registers[32]` | `uint32_t[]` | Banco de registradores x0–x31 |
| `mhartid` | `uint32_t` | ID do núcleo (carregado em `a0` = x10) |
| `instruction_memory` | `vector<uint32_t>` | Memória de instruções local |
| `vram_ref` | `VRAM&` | Referência compartilhada à VRAM |

O núcleo implementa o pipeline em três estágios dentro de `step()`:

```
FETCH   → lê instruction_memory[pc/4]
DECODE  → extrai opcode, rd, rs1, rs2, funct3, funct7, imediatos
EXECUTE → executa e atualiza registradores / VRAM / PC
```

O modelo é **MIMD** (Multiple Instruction, Multiple Data): cada núcleo executa o mesmo binário do shader, mas sobre dados diferentes determinados pelo seu `mhartid`.

#### ISA implementada

| Formato | Instruções |
|---|---|
| **Tipo-R** | ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND |
| **Tipo-I** | ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI |
| **Tipo-I (load)** | LB, LH, LW, LBU, LHU (lê da VRAM via MMIO) |
| **Tipo-S** | SW (escreve na VRAM via MMIO) |
| **Tipo-B** | BEQ, BNE, BLT, BGE, BLTU, BGEU |
| **Tipo-U** | LUI, AUIPC |
| **Tipo-J** | JAL, JALR |
| **Sistema** | ECALL (encerra o núcleo) |

> x0 é hardwired zero: qualquer escrita em `registers[0]` é revertida após cada instrução.

### 3. Barramento MMIO (`RV32ICore.cpp` linhas 127–146)

Os núcleos não têm instruções especiais de vídeo. Eles usam as instruções padrão **LW** e **SW** da ISA em endereços que são mapeados diretamente para a VRAM — este é o conceito de **Memory-Mapped I/O (MMIO)**:

```
Instrução SW executada pelo núcleo
         │
         ▼
case 0x23 (opcode Store):
    addr = registers[rs1] + imm_S(inst)
    vram_ref.write_pixel(addr, registers[rs2])
```

O endereço usado pelo shader é `base_addr + i`, onde `base_addr = mhartid * pixels_per_core`. Isso garante que cada núcleo escreva exclusivamente na sua faixa da VRAM, sem colisões.

### 4. VRAM e Double Buffering (`VRAM.h` + `GPUManager`)

#### VRAM

Memória de vídeo linear: cada posição armazena um pixel no formato **RGBA8888** (32 bits).

```
Índice:   0          1          2       ...    1023
         ┌──────────┬──────────┬──────────┬──────────┐
VRAM:    │ RRGGBBAA │ RRGGBBAA │ RRGGBBAA │ RRGGBBAA │
         └──────────┴──────────┴──────────┴──────────┘
          Core 0                             Core 63
          (px 0–15)                          (px 1008–1023)
```

A escrita é **lock-free**: como cada núcleo opera em faixas disjuntas calculadas pelo `mhartid`, não há condição de corrida e nenhuma trava (`mutex`) é necessária.

#### Double Buffering

```
                  ┌──────────────┐
  Núcleos 0–63 ──►│  back_buffer │ (invisível ao usuário)
  escrevem aqui   └──────┬───────┘
                         │ swap_buffers()
                         │  std::swap(back.memory, front.memory)
                         │  std::fill(back → zeros)   ← limpa para próximo frame
                         ▼
                  ┌──────────────┐
  Usuário vê ────►│ front_buffer │ (frame completo e consistente)
                  └──────────────┘
```

O usuário nunca vê um frame sendo construído pela metade. Somente após todos os núcleos terminarem (`future::get()`) é que `swap_buffers()` é chamado, tornando o frame visível atomicamente.

---

## Shader de gradiente (`build_gradient_shader`)

O shader é um programa **escrito em binário RV32I** diretamente em C++, usando o mini-assembler no namespace `RV32Asm`. O resultado é um `vector<uint32_t>` com 18 instruções que é copiado para a memória de instrução de cada núcleo.

### Algoritmo (pseudocódigo)

```
// Executado por cada núcleo independentemente
a0  = mhartid                      // preenchido pelo construtor

t0  = pixels_per_core (= 16)       // LUI + ADDI
t5  = log2(pixels_per_core) (= 4)  // ADDI
t1  = mhartid << 4                 // SLL  → base_addr = mhartid * 16

// Cálculo de cor somente com soma (ADD / ADDI):
//   cor = mhartid * 1024 + 255
//   mhartid * 1024 via 10 duplicações sucessivas (ADD t3, t3, t3)
//   O valor final em 32 bits coloca B = mhartid*4 nos bits 15:8
//   e A = 255 nos bits 7:0
t3  = mhartid * 2                  // ADD
t3  = t3 * 2   (= mhartid * 4)    // ADD
...  (10 duplicações no total)
t3  = mhartid * 1024               // ADD
t2  = t3 + 255                     // ADDI  → cor = mhartid*1024 + 255

t3  = 0  (i = 0)

loop:
    t4 = base_addr + i             // ADD
    VRAM[t4] = cor                 // SW   (MMIO)
    i++                            // ADDI
    if i != pixels_per_core: goto loop  // BNE

ECALL                              // encerra o núcleo
```

### Mapa de registradores

| Registrador | ABI | Valor |
|---|---|---|
| x10 | a0 | mhartid (ID do núcleo) |
| x5  | t0 | pixels_per_core (16) |
| x6  | t1 | base_addr = mhartid × 16 |
| x7  | t2 | cor RGBA calculada |
| x28 | t3 | contador do loop (i) |
| x29 | t4 | endereço VRAM atual |
| x30 | t5 | temporário para shifts |

### Gradiente de cores (preto → azul, somente soma)

Fórmula: `cor = mhartid × 1024 + 255`

| Núcleo | Cálculo | B (byte azul) | Cor RGBA | Visual |
|---|---|---|---|---|
| 0  | 0×1024+255   | 0   | `0x000000FF` | Preto opaco |
| 16 | 16×1024+255  | 64  | `0x000040FF` | Azul escuro |
| 32 | 32×1024+255  | 128 | `0x000080FF` | Azul médio  |
| 48 | 48×1024+255  | 192 | `0x0000C0FF` | Azul claro  |
| 63 | 63×1024+255  | 252 | `0x0000FCFF` | Azul pleno  |

---

## Visualização SDL2

Após o processamento, abre uma janela com dois painéis lado a lado:

```
┌─────────────────────────┬─┬─────────────────────────┐
│  faixa VERMELHA (ANTES) │ │  faixa VERDE  (DEPOIS)  │
├─────────────────────────┤ ├─────────────────────────┤
│                         │ │ ░░▒▒▓▓████████████████ │
│     (tela preta)        │ │ ░░▒▒▓▓████████████████ │
│     VRAM = zeros        │ │  preto → azul pleno     │
└─────────────────────────┴─┴─────────────────────────┘
  64 × 16 pixels × escala 10    separador de 4px
  Largura total: 1284px  ·  Altura: 184px
```

- Cada coluna = 1 núcleo (64 colunas no total)
- Cada linha = 1 pixel dentro da fatia do núcleo (16 linhas)
- Escala 10×: cada pixel da VRAM ocupa 10×10 px na tela
- Fechar: **ESC** ou botão de fechar da janela

---

## Como modificar

### Mudar resolução / número de núcleos

Em `main()`:

```cpp
const size_t NUM_CORES    = 64;    // quantidade de núcleos
const size_t TOTAL_PIXELS = 1024;  // deve ser múltiplo de NUM_CORES
```

> `PIXELS_PER_CORE` precisa ser potência de 2 para que o cálculo de `base_addr` via `SLL` funcione corretamente.

### Mudar as cores do gradiente

Em `build_gradient_shader()`, substitua o bloco de montagem da cor. Exemplos:

**Gradiente preto → vermelho:**
```cpp
prog.push_back(ADDI(30, 0, 2));    // t5 = 2
prog.push_back(SLL (7, 10, 30));   // t2 = mhartid << 2  (R: 0..252)
prog.push_back(ANDI(7,  7, 0xFF));
prog.push_back(ADDI(30, 0, 24));   // t5 = 24
prog.push_back(SLL (7,  7, 30));   // t2 = R << 24  → bits 31:24
prog.push_back(ADDI(28, 0, 0xFF)); // A = 0xFF
prog.push_back(OR  (7,  7, 28));   // t2 |= A
```

**Gradiente preto → verde:**
```cpp
prog.push_back(ADDI(30, 0, 2));
prog.push_back(SLL (7, 10, 30));   // G: 0..252
prog.push_back(ANDI(7,  7, 0xFF));
prog.push_back(ADDI(30, 0, 16));   // G no byte bits 23:16
prog.push_back(SLL (7,  7, 30));
prog.push_back(ADDI(28, 0, 0xFF));
prog.push_back(OR  (7,  7, 28));
```

O formato RGBA na VRAM é `0xRRGGBBAA`:
- **R** → bits 31:24 (shift 24)
- **G** → bits 23:16 (shift 16)
- **B** → bits 15:8  (shift 8)
- **A** → bits 7:0   (sem shift)

---

## Fluxo de execução completo

```
main()
 │
 ├─ GPUManager(64 núcleos, 1024 pixels)
 │    └─ inicializa back_buffer[1024] = 0
 │    └─ inicializa front_buffer[1024] = 0
 │    └─ cria RV32ICore[0..63], cada um com referência ao back_buffer
 │
 ├─ before_snapshot = gpu.get_pixels()   → cópia do front_buffer (zeros)
 │
 ├─ build_gradient_shader(64, 1024)      → 18 instruções RV32I binárias
 ├─ gpu.load_program(shader)             → copia para instruction_memory de cada núcleo
 │
 ├─ gpu.dispatch_frame()
 │    ├─ batch 0: cores 0–7  → 8 std::async → executam shader → escrevem no back_buffer
 │    ├─ batch 1: cores 8–15 → ...
 │    ├─ ...
 │    ├─ batch 7: cores 56–63
 │    └─ swap_buffers()
 │         ├─ std::swap(back_buffer.memory, front_buffer.memory)
 │         └─ std::fill(back_buffer → zeros)
 │
 ├─ after_snapshot = gpu.get_pixels()    → cópia do front_buffer (gradiente pronto)
 │
 ├─ gpu.print_diagnostics()              → lê front_buffer, imprime estatísticas
 │
 └─ visualize_before_after(before, after, 64, 16)
      └─ janela SDL2: painel ANTES (preto) | painel DEPOIS (gradiente azul)
```

---

## Limitações e trabalhos futuros

| Item | Status | Observação |
|---|---|---|
| ISA RV32I inteira | Implementada | Todos os formatos R/I/S/B/U/J |
| Multiplicação (RV32M) | Não implementada | Shader usa shifts para multiplicar por pot. de 2 |
| Double buffering | Implementado | `std::swap` real entre back e front |
| Renderização complexa | Não implementada | Apenas shaders de cor sólida por núcleo |
| Síntese HLS (FPGA) | Trabalho futuro | Base C++ compatível com ferramentas HLS |
| Extensão vetorial (RV32V) | Trabalho futuro | Permitiria SIMD por núcleo |
| Texto na janela SDL2 | Não implementado | Requereria SDL2_ttf |
