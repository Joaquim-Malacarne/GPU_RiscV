#ifndef JANELA_H
#define JANELA_H

#include <cstdint>
#include <string>
#include <vector>
#include <SDL2/SDL.h>

/*
 * Janela — componente gráfico SDL2.
 *
 * Encapsula janela, renderer e textura.
 * Recebe pixels no formato RRGGBBAA (igual ao da VRAM) e exibe na tela.
 * main.cpp não precisa incluir SDL2 diretamente.
 */
class Janela {
public:
    // Cria janela com resolução fb_w×fb_h escalada por `scale`
    Janela(const char* title, int fb_w, int fb_h, int scale);
    ~Janela();

    // Processa eventos: retorna false se o usuário fechou ou pressionou ESC
    bool poll_events();

    // Converte pixels RRGGBBAA → SDL ARGB8888 e renderiza na tela
    void draw(const std::vector<uint32_t>& pixels);

private:
    int fb_w, fb_h;
    SDL_Window*           win;
    SDL_Renderer*         ren;
    SDL_Texture*          tex;
    std::vector<uint32_t> sdl_buf;
    std::string           base_title;
    int                   fps_frames = 0;
    Uint32                fps_last   = 0;
};

#endif // JANELA_H
