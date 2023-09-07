#pragma once

#ifndef GB_H
#define GB_H

#include "alu_binary.h"
#include "colors.h"

u8  cgb_mode;
u8  gb_frameskip; // only draw the screen when vblank_counter % gb_frameskip == 0
u8  gb_debug_show_tracelog;
u8  gb_cgb_compability_palette_flag;

// Initialize emulator components
int gb_init(u8* rom_buffer);
void gb_powerup();

// Advance the emulator by one frame, taking in the input data from the device
// returns 1 if screen needs redrawing, 0 otherwise.
int gb_update(u8* inputs);

// This emulates 1 M-cycle / 4 T-cycles
void tick();

// Fetch a pointer to the PPU's screen buffer
RGBColor* gb_get_screen_buffer();

void gb_output_audio_sample(u8 output);

void gb_cleanup();

#endif GB_H