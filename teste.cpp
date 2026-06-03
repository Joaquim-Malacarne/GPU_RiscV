#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Initialize SDL2 Video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cout << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // 2. Create the window
    SDL_Window* window = SDL_CreateWindow(
        "SDL2 Window Title",                  // Window title
        SDL_WINDOWPOS_CENTERED,               // Initial X position
        SDL_WINDOWPOS_CENTERED,               // Initial Y position
        800,                                  // Width in pixels
        600,                                  // Height in pixels
        SDL_WINDOW_SHOWN                      // Flags (e.g., SDL_WINDOW_RESIZABLE)
    );

    if (window == nullptr) {
        std::cout << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // 3. Keep the window open with an event loop
    bool isRunning = true;
    SDL_Event event;

    while (isRunning) {
        // Poll for events so the OS doesn't think the window crashed
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {     // User clicked the 'X' button
                isRunning = false;
            }
        }
    }

    // 4. Clean up resources and close
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
