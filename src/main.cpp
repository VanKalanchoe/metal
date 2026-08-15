#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <memory>

#include "mtl_renderer.h"

#include <imgui_impl_sdl3.h>

static SDL_Window *window = nullptr;
static std::unique_ptr<NRI::MTLRenderer> renderer;

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("Example Renderer Clear", "1.0", "com.example.renderer-clear");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    window = SDL_CreateWindow("examples/renderer/clear", 640, 480, SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    
    if (window == NULL)
    {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    SDL_SetWindowMinimumSize(window, 640, 480);
    
    renderer = std::make_unique<NRI::MTLRenderer>(*window);
    
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }
    
    if(event->type == SDL_EVENT_KEY_DOWN)
    {
        if(event->key.scancode == SDL_SCANCODE_ESCAPE) return SDL_APP_SUCCESS;
    }
    
    if(event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        if(renderer)
        {
            renderer->updateViewportSize(event->window.data1, event->window.data2);
        }
    }
    
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    renderer->draw();
    
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
}
