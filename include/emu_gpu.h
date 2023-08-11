#pragma once

#ifndef EMU_GPU_H
#define EMU_GPU_H

#include <SDL.h>
#include "alu_binary.h"

// Define an enumeration for buffer types
typedef enum BufferType {
    SPRITE_BUFFER,
    BACKGROUND_BUFFER,
    WINDOW_BUFFER
} BufferType;

int emu_gpu_init();

u8* emu_gpu_get_pixel_buffer();

void emu_gpu_cleanup();

void emu_gpu_set_pixel(int x, int y, u8 color_index);

void emu_gpu_draw_tile(int x, int y, u8* tile_data, BufferType buffer_type);

#endif EMU_GPU_H