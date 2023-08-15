#include "application.h"
#include "macros.h"


#include <stdio.h>
#include <string.h>

#include <SDL.h>
#include <GL/glew.h>

#include <SDL_opengl.h>

#include "graphics.h"
#include "emu_cpu.h"
#include "emu_gpu.h"


SDL_Window* window = NULL;
SDL_Event   window_event;
const Uint8* kb_state;

int         fps = 60.0;
float       tick_rate = 60.0 / 60.0; // Milliseconds per frame

int         window_scale = 4;
u8          redraw_flag = 1;


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

/// <summary>
/// Stores the contents of a file into a buffer and returns a pointer to it.
/// </summary>
u8* LoadROM(const char* fname) {
    u8*     buffer;
    size_t  bytes_read;
    long    buffer_size;
    FILE*   fp;

    fp = fopen(fname, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open file: %s\n", fname);
        perror("Error: ");
        return NULL;
    }
    // Get the file size
    fseek(fp, 0L, SEEK_END);
    buffer_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    // Copy the file contents to a buffer
    buffer = (u8*)malloc(sizeof(u8) * buffer_size);
    if (buffer == NULL)
    {
        perror("Error: ");
        fclose(fp);
        return NULL;
    }
    
    bytes_read = fread(buffer, 1, buffer_size, fp);
    if (bytes_read != (size_t)buffer_size)
    {
        perror("Error: ");
        free(buffer);
        fclose(fp);
        return NULL;
    }

    /*
    for (int i = 0; i < sz; i++)
    {
        if (i % 16 == 0) printf("\n");
        printf("%02X ", buffer[i]);
    }
    */


    fclose(fp);
    return buffer;
    
}

int application_init(const char* title) {
    
    u8* rom_buffer;

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
    //SDL_SetEventFilter(EventFilter, NULL); // NULL for user data
    int num_keys;
    kb_state = SDL_GetKeyboardState(&num_keys);
    
    // Initialize OpenGL stuff
    if (!graphics_init(window))
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Open rom
    //rom_buffer = LoadROM("C:/dev/AluBoy/AluBoy/resources/roms/Pokemon Red.gb");
    rom_buffer = LoadROM("C:/dev/AluBoy/AluBoy/resources/roms/02-write_timing.gb");
    //u8* rom_buffer = LoadROM("C:/dev/AluBoy/AluBoy/resources/roms/start_inc_1_cgb04c_out1E.gbc");
    if (rom_buffer == NULL)
    {
        graphics_cleanup();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Initialize emulator cpu
    if (emu_cpu_init(rom_buffer) == -1)
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

    printf("100 to signed: %d\n",   (s8)((u8)100));
    printf("200 to signed: %d\n",   (s8)((u8)200));
    printf("255 to signed: %d\n",   (s8)((u8)255));
    printf("0 to signed: %d\n",     (s8)((u8)0));
    

    return 0;
}


void application_update() {
    
    u32  current_time     = 0;
    u32  delta            = 0;
    u32  timer_total      = 0; // Total game time
    u32  last_frame_time  = SDL_GetTicks(); // Set to current time when difference is bigger than 1/60
    u8   keep_window_open = 1;

    while (keep_window_open) {

        // Poll events while queue isn't empty (uses a filter, see EventFilter function)
        while (SDL_PollEvent(&window_event)) {
            switch (window_event.type) {
            case SDL_QUIT:
                keep_window_open = 0;
                break;

            case SDL_MOUSEMOTION:
            {
                int x = window_event.motion.x / window_scale;
                int y = window_event.motion.y / window_scale;
                //printf("%d,%d\n", x, y);
                emu_gpu_set_pixel(x, y, 1);
                redraw_flag = 1;
            }
            break;
            case SDL_KEYDOWN:
                //
                break;
            }
        }
        // Array of pressed inputs, which is later passed to the emulator.
        u8 inputs[8] = { 
            kb_state[SDL_SCANCODE_RIGHT], 
            kb_state[SDL_SCANCODE_LEFT], 
            kb_state[SDL_SCANCODE_UP],
            kb_state[SDL_SCANCODE_DOWN],
            kb_state[SDL_SCANCODE_X],
            kb_state[SDL_SCANCODE_Z],
            kb_state[SDL_SCANCODE_A],
            kb_state[SDL_SCANCODE_S]
        };

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
        emu_cpu_update(&inputs);

        // Draw
        application_draw();

        last_frame_time = current_time;
    }
}

void application_draw() {
    if (!redraw_flag) return; // Only draws when necessary

    graphics_update_rgba_buffer(emu_gpu_get_pixel_buffer());
    graphics_draw(window);

    redraw_flag = 0; // Reset the flag
}

void application_cleanup() {
    emu_cpu_cleanup();
    emu_gpu_cleanup();
    graphics_cleanup();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();

}

