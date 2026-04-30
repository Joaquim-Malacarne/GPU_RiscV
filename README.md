============================================================
   GPU-V: EMULADOR DE GPU BASEADO EM RISC-V (WINDOWS)
============================================================

SOBRE O PROJETO:
------------------------------------------------------------
O GPU-V é um emulador de unidade de processamento gráfico
(GPU) implementado em C++17, baseado na ISA RISC-V RV32I.

Em vez de um chip gráfico monolítico e proprietário, o GPU-V
modela cada núcleo de shader como uma instância autônoma de
uma CPU RV32I — seguindo o paradigma MIMD (Multiple
Instruction, Multiple Data). Os núcleos rodam em paralelo
via Thread Pool dinâmico e despejam pixels na VRAM de
forma lock-free, garantindo performance sem travar o sistema.

COMPONENTES PRINCIPAIS:
  - GPU Manager   : Orquestra os núcleos via Thread Pool
  - Núcleos RV32I : Instâncias independentes (PC + x0-x31)
  - Barramento    : Roteamento de memória via MMIO
  - VRAM          : Buffer de pixels RGBA8888 com Double Buffering


PRÉ-REQUISITOS:
------------------------------------------------------------
1. Você precisa de um compilador C++ com suporte a C++17.
   (MinGW/GCC >= 8.0, instalado via MSYS2, Dev-C++ ou
    Code::Blocks com MinGW integrado).

2. Certifique-se de que todos os arquivos (.cpp, .h) estão
   na mesma pasta do projeto.


PASSO 1: ABRIR O TERMINAL
------------------------------------------------------------
1. Abra a pasta do projeto no Explorador de Arquivos.
2. Na barra de endereço (onde aparece o caminho da pasta),
   digite "cmd" e aperte ENTER.
   Isso abrirá o Prompt de Comando já na pasta certa.


PASSO 2: COMPILAR
------------------------------------------------------------
Copie e cole o comando abaixo no terminal e aperte ENTER:

g++ main.cpp gpu_manager.cpp core.cpp bus.cpp ram.cpp -o gpu_v.exe -std=c++17 -pthread

(Se não aparecer nenhuma mensagem de erro, funcionou.)


PASSO 3: EXECUTAR
------------------------------------------------------------
Digite o comando abaixo e aperte ENTER:

gpu_v.exe


COMO FUNCIONA A EXECUÇÃO:
------------------------------------------------------------
1. O GPU Manager instancia N núcleos RV32I.
2. Cada núcleo lê seu ID único via registrador 'mhartid'.
3. Com base nesse ID, o núcleo calcula qual região do
   framebuffer ele deve processar.
4. Os núcleos escrevem pixels na VRAM (Back Buffer)
   de forma paralela e lock-free.
5. Quando todos terminam, os buffers são trocados (swap)
   e a imagem final aparece no Front Buffer.


MAPA DE MEMÓRIA (MMIO):
------------------------------------------------------------
  0x00000000 - 0x0007FFFF  : RAM Principal (512 KB)
  0x10000000 - 0x1007FFFF  : VRAM (framebuffer, 512 KB)
  0x20000000 - 0x20000FFF  : Periféricos (controle/status)


RESULTADOS:
------------------------------------------------------------
- O emulador exibirá o progresso de cada núcleo no terminal.
- Ao final da renderização, a imagem será escrita na VRAM.
- Logs de erro de acesso inválido à memória são impressos
  em stderr para facilitar o debug.


ESTRUTURA DO PROJETO:
------------------------------------------------------------
  main.cpp        : Ponto de entrada e configuração inicial
  gpu_manager.cpp : Thread Pool e orquestração dos núcleos
  gpu_manager.h   : Interface do GPU Manager
  core.cpp        : Implementação da CPU RV32I (núcleo)
  core.h          : Interface do núcleo RV32I
  bus.cpp         : Barramento de memória (MMIO)
  bus.h           : Interface do barramento
  ram.cpp         : RAM principal e VRAM
  ram.h           : Interface das memórias e periféricos
