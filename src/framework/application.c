#include "application.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>

#include <SDL.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include <alu_io.h>

#include "graphics.h"
#include "audio.h"
#include "gb.h"



SDL_Window* window = NULL;
SDL_Event   window_event;
const Uint8* kb_state;

int     sdl_initialized     = 0;
int     gl_initialized      = 0;
int     audio_initialized   = 0;
int     gb_initialized      = 0;


int     window_scale = 4;
int     fps = 60;
double  tick_rate; // Milliseconds per frame

// shared variables
const char* rom_file_name = "metroid ii";
const char* rom_file_path = "C:/dev/AluBoy/AluBoy/resources/roms/games/";

/* 
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
*/

int application_init(const char* title) {
    const char* path_arr[] = { rom_file_path, rom_file_name, ".gb"};

    char*   rom_path    = NULL;
    u8*     rom_buffer  = NULL;
    int     num_keys;

    // Initialize SDL Video & Audio
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
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
        SDL_Quit();
        return -1;
    }

    sdl_initialized = 1;

    // Set the event filter
    //SDL_SetEventFilter(EventFilter, NULL); // NULL for user data

    // Pointer to an array of key states. Only needs to be set once.
    kb_state = SDL_GetKeyboardState(&num_keys);
    
    // Initialize OpenGL stuff
    gl_initialized = graphics_init(window);
    if (!gl_initialized)
    {
        application_cleanup();
        return -1;
    }

    // Initialize audio stuff
    audio_initialized = audio_init();
    if (!audio_initialized) {
        application_cleanup();
        return -1;
    }

    // Load ROM
    rom_path = combine_strings(path_arr, 3);
    if (rom_path != NULL) {
        rom_buffer = LoadBuffer(rom_path);
        free(rom_path);
    }
    if (rom_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for ROM buffer!\n");
        application_cleanup();
        return -1;
    }

    // Initialize emulator
    gb_initialized = gb_init(rom_buffer);
    if (!gb_initialized) {
        fprintf(stderr, "Failed to initialize emulator!\n");
        application_cleanup();
        return -1;
    }
    gb_powerup();

    tick_rate = 1000.0 / (fps * gb_frameskip);

    return 0;
}

void application_update() {
    
    u32     current_time        = 0;
    double  delta               = 0;
    double  timer_total         = 0;                // Total game time
    u32     last_frame_time     = SDL_GetTicks();   // Set to current time when difference is bigger than 1/60
    u8      keep_window_open    = 1;

    // DEBUG FPS calculation
    u32     total_frames        = 0;
    u32     start_time          = SDL_GetTicks();

    while (keep_window_open) {
        u8 inputs[8];
        u8 redraw_flag;

        current_time = SDL_GetTicks();
        delta += current_time - last_frame_time;
        if (delta > 100) delta = 100.0;
        last_frame_time = current_time;

        // Stalls the program when its running too fast
        if (tick_rate > delta) {
            SDL_Delay((u32)(tick_rate - delta));
        }
        // Alternative: More accurate, but cpu works harder since it never sleeps
        //if (tick_rate > delta) continue; 

        delta -= tick_rate;

        ////////////////////////////////////////////
        // This part of the code aims to execute at ~60 fps

        // Poll events while queue isn't empty (uses a filter, see EventFilter function)
        while (SDL_PollEvent(&window_event)) {
            switch (window_event.type) {
            case SDL_QUIT:
                keep_window_open = 0;
                continue;
            case SDL_MOUSEMOTION:
            {
                //int x = window_event.motion.x / window_scale;
                //int y = window_event.motion.y / window_scale;
                //printf("%d,%d\n", x, y);
            }
            break;
            case SDL_KEYDOWN:
                switch (window_event.key.keysym.sym) {
                    // Restart emulator
                    case SDLK_r:
                        gb_powerup();
                        break;
                    // Turbo mode
                    case SDLK_t:
                        gb_frameskip = (gb_frameskip == 1) ? 3 : 1;
                        // adjust tick rate to the new speed
                        tick_rate = 1000.0 / (fps * gb_frameskip);
                        break;
                    // Toggle audio ON/OFF
                    case SDLK_m:
                        audio_toggle();
                        break;
                    // Write trace log ON/OFF
                    case SDLK_BACKQUOTE:
                        gb_debug_show_tracelog = (gb_debug_show_tracelog == 1) ? 0 : 1;
                        break;
                }
                break;
            }
        }
        // Array of pressed inputs, which is later passed to the emulator.
        inputs[0] = kb_state[SDL_SCANCODE_RIGHT];
        inputs[1] = kb_state[SDL_SCANCODE_LEFT];
        inputs[2] = kb_state[SDL_SCANCODE_UP];
        inputs[3] = kb_state[SDL_SCANCODE_DOWN];
        inputs[4] = kb_state[SDL_SCANCODE_X];
        inputs[5] = kb_state[SDL_SCANCODE_Z];
        inputs[6] = kb_state[SDL_SCANCODE_A];
        inputs[7] = kb_state[SDL_SCANCODE_S];

        // Update frame logic
        timer_total += tick_rate;

        // Run one frame of the emulator, returns 1 if screen needs redrawing, 0 otherwise.
        redraw_flag = gb_update((u8*) & inputs);

        // Draw
        if (redraw_flag) application_draw();



        // DEBUG FPS calculation
        total_frames++;
        // At the end of your loop:
        u32 end_time = SDL_GetTicks();
        u32 elapsed_time = end_time - start_time;
        if (elapsed_time > 1000) {
            start_time = end_time;
            total_frames = 0;
        }
        // Calculate average FPS
        double average_fps = total_frames / (elapsed_time / 1000.0); // Divide by 1000 to convert milliseconds to seconds
        // Print or use the average_fps value as needed
        if (total_frames > 0 && total_frames % 20 == 0) {
            char str[6];
            sprintf(str, "%d", (u32)average_fps);
            SDL_SetWindowTitle(window, str);
        }
    }
}

void application_draw() {
    // Converts the emulator's screen buffer into RGBA format
    graphics_update_rgba_buffer(gb_get_screen_buffer(), SCREEN_WIDTH * SCREEN_HEIGHT);
    // Display the buffer's contents on the SDL screen
    graphics_draw(window);
}

void application_cleanup() {
    if (gb_initialized)     gb_cleanup();
    if (audio_initialized)  audio_cleanup();
    if (gl_initialized)     graphics_cleanup();

    if (sdl_initialized) {
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
}

