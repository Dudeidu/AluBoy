#pragma once

#ifndef EMU_GPU_H
#define EMU_GPU_H

#include <stdint.h>
#include <SDL.h>

int emu_gpu_create_pixel_buffer();

int8_t* emu_gpu_get_pixel_buffer();

void emu_gpu_cleanup();

void emu_gpu_set_pixel(int x, int y, uint8_t color_index);


#endif EMU_GPU_H