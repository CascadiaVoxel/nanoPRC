#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h> // Includes cross-platform GLES profiles
#include <prc_api.h>
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Initialize SDL Subsystems
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 2. Request an OpenGL ES 3.0 Rendering Context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // 3. Create the Window Layer (Adapts dynamically to mobile phone bounds)
    SDL_Window* window = SDL_CreateWindow(
        "nanoPRC 3D PDF Viewer", 
        800, 600, 
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "GLES Context creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // 4. Fire up shared core parser library context
    prc_context* prcCtx = prc_api_new_context(nullptr);
    if (prcCtx) {
        std::cout << "nanoPRC parser context successfully active inside mobile loop!" << std::endl;
    }

    // 5. Main Application Loop
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            // Mobile specific touch triggers
            else if (event.type == SDL_EVENT_FINGER_DOWN) {
                // event.tfinger.x and event.tfinger.y provide normalized 0..1 touch inputs!
            }
        }

        // Render pass clearing loop (GLES 3.0 standard commands)
        glClearColor(0.1f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        SDL_GL_SwapWindow(window);
    }

    // Clean up resources cleanly
    if (prcCtx) {
        prc_api_release_context(prcCtx);
    }
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}