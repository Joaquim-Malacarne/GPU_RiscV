# GPU-V — Emulador de GPU com núcleos RISC-V

Projeto acadêmico — Universidade Católica de Santos

Emula uma GPU com **64 núcleos RV32I** paralelos renderizando um cubo 3D rotacionando. Toda a matemática gráfica (rotação, projeção, rasterização) roda nos núcleos RISC-V; a CPU apenas dispara os shaders.

---

## Compilar e rodar

```bash
make
./gpu_v
# Pressione ESC ou feche a janela para sair
```

**Dependência:** `libsdl2-dev`

---

## Configurações (`config.h`)

| Variável       | Padrão | Descrição                              |
|----------------|--------|----------------------------------------|
| `FB_W / FB_H`  | 128    | Resolução do framebuffer               |
| `SCALE`        | 5      | Fator de escala da janela SDL          |
| `NUM_CORES`    | 64     | Número de núcleos RV32I                |
| `NUM_THREADS`  | 0      | Threads do host (0 = automático)       |

---

## Arquitetura resumida

```
Por frame:
  CPU: grava angle_idx na VRAM
  Núcleo 0 (geometry shader): rotação → projeção → Bresenham → edge mask
  Núcleos 0–63 (fragment shader): lê mask → pinta pixel azul ou preto
  SDL2: exibe o framebuffer
```

---

## Estrutura de arquivos

| Arquivo                | Função                                          |
|------------------------|-------------------------------------------------|
| `config.h`             | Constantes configuráveis                        |
| `vram_layout.h`        | Offsets da VRAM                                 |
| `RV32ICore.h/.cpp`     | Emulador do núcleo RV32I+M                      |
| `RV32Asm.h`            | Mini-montador inline                            |
| `VRAM.h`               | Memória da GPU                                  |
| `gpu.h/.cpp`           | Gerenciador de núcleos e threads                |
| `geom_shader.h/.cpp`   | Shader de geometria (540 instruções RV32I)      |
| `frag_shader.h/.cpp`   | Shader de fragmento (19 instruções RV32I)       |
| `janela.h/.cpp`        | Janela SDL2                                     |
| `main.cpp`             | Loop principal                                  |
| `explicacao.md`        | Documentação técnica detalhada                  |

---

## Instruções implementadas

Base **RV32I** completo + extensão **M** (MUL, MULH, DIV, REM e variantes).

Para documentação técnica detalhada veja [`explicacao.md`](explicacao.md).
