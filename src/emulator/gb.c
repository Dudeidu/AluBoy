#include "gb.h"
#include "macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_shared.h"
#include "mmu.h"
#include "cpu.h"
#include "input.h"
#include "timer.h"
#include "ppu.h"
#include "apu.h"

#define AUTOSAVE_INTERVAL 18000 // frame interval between automatic saves

int mmu_initialized     = 0;
int cpu_initialized     = 0;
int ppu_initialized     = 0;

int cycles_this_update  = 0;

u8  cgb_mode;
u8  save_enabled;       // if true, save at the end of this frame
int autosave_counter;

// Tests
int emu_seconds          = 0;

// Forward declarations
int check_redraw();

// PUBLIC ------------------------------------------------

int gb_init(u8* rom_buffer) {
    mmu_initialized = mmu_init(rom_buffer);
    cpu_initialized = cpu_init(rom_buffer);
    ppu_initialized = ppu_init();
    apu_init();

    if (!mmu_initialized || !cpu_initialized || !ppu_initialized) {
        gb_cleanup();
        return 0;
    }

    autosave_counter = 0;

    return 1;
}

int gb_update(u8* inputs) {
    
    input_update(inputs);
    
    // This loop emulates 1 frame
    cycles_this_update = 0;
    while (cycles_this_update < MAXDOTS)
    {
        u8 cycles;
        u8 ly_start = reg[REG_LY];

        cycles = cpu_update();
  
        cycles_this_update += cycles;
        
        // vsync - end frame every full screen cycle to avoid tearing
        if (lcd_enabled && ly_start != reg[REG_LY] && reg[REG_LY] == 0) break;
    }
    
    // Autosave
    if (has_battery) {
        autosave_counter++;
        if (autosave_counter >= AUTOSAVE_INTERVAL) {
            save();
            autosave_counter = 0;
        }
    }

    return check_redraw();
}

// This emulates 1 M-cycle.
void tick() {
    u16 clock_prev = internal_counter;

    // OAM DMA transfer is running
    if (dma_transfer_flag) dma_transfer_tick();

    input_tick();
    timer_tick();
    ppu_tick();
    
    // “DIV-APU” counter / frame sequencer is increased every time DIV’s bit 4 (5 in double-speed mode) goes from 1 to 0, 
    // TODO add support for double speed mode (changes apu_clock_bit to 13 instead of 12)
    if (GET_BIT(clock_prev, apu_clock_bit) && !GET_BIT(internal_counter, apu_clock_bit)) {
        apu_frame_sequencer_update();
        //printf("clock prev: %02X, clock new: %02X\n", (clock_prev >> 8) & 0xFF, reg[REG_DIV]);
    }
    apu_tick();
    
    if (stat_bug) stat_bug = 0;
}

u8* gb_get_screen_buffer() {
    return ppu_get_pixel_buffer();
}

void gb_cleanup() {
    if (ppu_initialized) ppu_cleanup();
    if (mmu_initialized) mmu_cleanup();
}

// PRIVATE ------------------------------------------------

// Emulator only draws when the PPU's buffer has changed, the redraw flag is toggled off when returns true
int check_redraw() {
    int flag = ppu_get_redraw_flag();
    if (flag) ppu_set_redraw_flag(0);
    
    return flag;
}