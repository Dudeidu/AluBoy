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


SDL_Window* window = NULL;
SDL_Event   window_event;

int         fps = 60.0;
float       tick_rate = 1000.0 / 60.0; // Milliseconds per frame

int         window_scale = 4;
bool        redraw = true;


int EventFilter(void* userdata, SDL_Event* event) {
    // Process SDL_QUIT event
    if (event->type == SDL_QUIT 
        || event->type == SDL_KEYDOWN 
        || event->type == SDL_MOUSEMOTION) {
        return 1; // Allow SDL_QUIT event to be processed
    }

    // Ignore other events
    return 0; // Ignore the event
}

int application_init(const char* title) {
    
    // Initialize SDL Video
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "%s\n", SDL_GetError());
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
        fprintf(stderr, "%s\n", SDL_GetError());
        return -1;
    }

    // Set the event filter
    SDL_SetEventFilter(EventFilter, NULL); // NULL for user data

    // Initialize OpenGL stuff
    if (!graphics_init(window))
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Initialize emulator cpu
    if (emu_cpu_init() == -1)
    {
        graphics_cleanup();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Initialize emulator gpu
    if (emu_gpu_init() == -1)
    {
        emu_cpu_cleanup();
        graphics_cleanup();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    return 0;
}


void application_update() {
    
    Uint32 current_time = 0;
    Uint32 delta = 0;
    Uint32 timer_total = 0;    // Total game time
    Uint32 last_frame_time = SDL_GetTicks();// Set to current time when difference is bigger than 1/60

    bool keep_window_open = true;
    while (keep_window_open) {

        // Poll events while queue isn't empty (uses a filter, see EventFilter function)
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
        // todo optimization: batch events (like keyboard inputs) together and only after
        // all events are polled process the inputs.

        // Stalls the program when its running too fast
        current_time = SDL_GetTicks();
        delta = current_time - last_frame_time;
        if (delta > 100) delta = 100;
        last_frame_time = current_time;

        if (tick_rate > delta)
        {
            SDL_Delay(tick_rate - delta);
            //printf("under: %d\n", (int)tick_rate - delta);
        }
        else
        {
            //printf("over: %d\n", delta - (int)tick_rate);
        }

        ////////////////////////////////////////////
        // This part of the code aims to execute at ~60 fps

        // Update frame logic
        timer_total += tick_rate;
            
        // Update cpu logic
        emu_cpu_update();

        // Draw
        application_draw();

        last_frame_time = current_time;
    }
}

void application_draw() {
    if (!redraw) return; // Only draws when necessary

    graphics_update_rgba_buffer(emu_gpu_get_pixel_buffer());
    graphics_draw(window);

    redraw = false; // Reset the flag
}

void application_cleanup() {
    emu_cpu_cleanup();
    emu_gpu_cleanup();
    graphics_cleanup();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();

}

