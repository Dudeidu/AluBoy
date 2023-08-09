#include "application.h"
#include "macros.h"

#include <stdbool.h>
#include <stdio.h>

#include <SDL.h>
#include <GL/glew.h>

#include <SDL_opengl.h>

#include "graphics.h"
#include "emu_cpu.h"
#include "emu_gpu.h"


SDL_Window*      window = NULL;
SDL_Event        window_event;

Uint64           last_frame_time = 0;// Set to current time when difference is bigger than 1/60
Uint64           timer_total = 0;    // Total game time
int              fps = 60.0;
float            tick_rate = 1000.0 / 60.0; // Milliseconds per frame

int              window_scale = 4;
bool             redraw = true;


int application_init(const char* title) {
    
    // Initialize SDL Video
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("%s\n", SDL_GetError());
        return -1;
    }
    // Create the main window
    window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, 
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * window_scale, 
        SCREEN_HEIGHT * window_scale, 
        SDL_WINDOW_OPENGL);
    if (!window)
    {
        printf("%s\n", SDL_GetError());
        return -1;
    }

    // Initialize OpenGL stuff
    if (!graphics_init(window))
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Initialize emulator gpu
    if (emu_gpu_create_pixel_buffer() == -1)
    {
        graphics_cleanup();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    return 0;
}

void application_cleanup() {
    emu_gpu_cleanup();
    graphics_cleanup();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();

}

void application_update() {
    bool keep_window_open = true;
    while (keep_window_open) {
        // Poll events while queue isn't empty
        while (SDL_PollEvent(&window_event)) {
            switch (window_event.type) {
            case SDL_QUIT:
                keep_window_open = false;
                break;
            
            case SDL_MOUSEMOTION:
                {
                    int x = window_event.motion.x / window_scale;
                    int y = window_event.motion.y / window_scale;
                    //printf("%d,%d\n", x, y);
                    emu_gpu_set_pixel(x, y, 1);
                    redraw = true;
                }
                break;
            case SDL_KEYDOWN:
                printf("%c\n", window_event.key.keysym.sym);
            }
        }

        // Calculate delta time
        Uint64 current_time = SDL_GetTicks64();
        int delta = current_time - last_frame_time;

        // Update frame logic
        if (delta >= tick_rate) {
            timer_total += tick_rate;
            // Update cpu logic
            emu_cpu_update();

            // Draw
            application_draw();

            last_frame_time = current_time;
        }
    }
}

void application_draw() {
    if (!redraw) return;

    //uint8_t* pixel_buffer = emu_gpu_get_pixel_buffer();
    graphics_update_rgba_buffer(emu_gpu_get_pixel_buffer());

    graphics_draw(window);

    redraw = false; // Reset the flag

}


