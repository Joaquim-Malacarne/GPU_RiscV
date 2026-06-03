#ifndef SHARED_FRAME_H
#define SHARED_FRAME_H

#include <cstdint>
#include <vector>
#include <mutex>
#include <condition_variable>

// Buffer compartilhado entre as threads de cálculo (produtoras) e a thread da
// tela (consumidora). Semântica de "último frame": o cálculo sobrescreve frames
// não consumidos; a tela sempre exibe o mais recente disponível.
struct SharedFrame {
    std::vector<uint32_t>   pixels;
    std::mutex              mtx;
    std::condition_variable cv;
    bool ready = false;
    bool stop  = false;
};

#endif // SHARED_FRAME_H
