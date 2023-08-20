#pragma once

#ifndef PPU_H
#define PPU_H

#include "alu_binary.h"

// Define an enumeration for buffer types
typedef enum BufferType {
    SPRITE_BUFFER,
    BACKGROUND_BUFFER,
    WINDOW_BUFFER
} BufferType;

int ppu_init();

u8* ppu_get_pixel_buffer();

u8 ppu_get_redraw_flag();
void ppu_set_redraw_flag(u8 val);

void ppu_tick(u8 cycles);

void ppu_cleanup();

#endif PPU_H