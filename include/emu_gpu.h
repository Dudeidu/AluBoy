#pragma once

#ifndef EMU_GPU_H
#define EMU_GPU_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#include "alu_binary.h"

// Define an enumeration for buffer types
typedef enum BufferType {
    SPRITE_BUFFER,
    BACKGROUND_BUFFER,
    WINDOW_BUFFER
} BufferType;

int emu_gpu_init();

int8_t* emu_gpu_get_pixel_buffer();

void emu_gpu_cleanup();

void emu_gpu_set_pixel(int x, int y, uint8_t color_index);

void emu_gpu_draw_tile(int x, int y, uint8_t* tile_data, BufferType buffer_type);

#endif EMU_GPU_H