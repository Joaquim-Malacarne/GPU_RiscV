#include "janela.h"
#include <iostream>
#include <string>

Janela::Janela(const char* title, int fb_w, int fb_h, int scale)
    : fb_w(fb_w), fb_h(fb_h), win(nullptr), ren(nullptr), tex(nullptr),
      sdl_buf((size_t)(fb_w * fb_h)), base_title(title), fps_last(SDL_GetTicks()) {

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[Janela] SDL_Init falhou: " << SDL_GetError() << "\n";
        return;
    }

    win = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        fb_w * scale, fb_h * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    // Mantém o framebuffer lógico fb_w×fb_h independente do tamanho da janela.
    // SDL escala automaticamente ao renderizar.
    SDL_RenderSetLogicalSize(ren, fb_w, fb_h);

    // ARGB8888: A nos bits 31-24, R 23-16, G 15-8, B 7-0
    tex = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        fb_w, fb_h);
}

Janela::~Janela() {
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

bool Janela::poll_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return false;
    }
    return true;
}

void Janela::draw(const std::vector<uint32_t>& pixels) {
    // Converte RRGGBBAA (formato VRAM) → AARRGGBB (SDL ARGB8888)
    for (size_t i = 0; i < sdl_buf.size(); ++i) {
        const uint32_t p = pixels[i];
        const uint8_t  r = (p >> 24) & 0xFF;
        const uint8_t  g = (p >> 16) & 0xFF;
        const uint8_t  b = (p >>  8) & 0xFF;
        sdl_buf[i] = (0xFFu << 24) | ((uint32_t)r << 16) |
                     ((uint32_t)g  <<  8) | b;
    }

    SDL_UpdateTexture(tex, nullptr, sdl_buf.data(), fb_w * (int)sizeof(uint32_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);

    ++fps_frames;
    Uint32 now = SDL_GetTicks();
    if (now - fps_last >= 1000) {
        std::string title = base_title + "  |  " + std::to_string(fps_frames) + " FPS";
        SDL_SetWindowTitle(win, title.c_str());
        fps_frames = 0;
        fps_last   = now;
    }
}
