#pragma once

#ifndef PPU_H
#define PPU_H

#include "alu_binary.h"

u8  oam_dma_transfer_flag;  // whether a dma transfer is currently running
u8  oam_dma_access_flag;    // allows bypassing the DMA memory blocking when its the DMA itself doing accessing the memory
u8  oam_dma_index;          // 0x00-0x9F

u8  stat_irq_flag;          // STAT register interrupt request
u8  stat_bug;               // if true, all STAT flags are enabled for 1 cycle

u8  lcd_enabled;
u8  lcd_mode;               // vblank/hblank/oam search/pixel rendering...

int ppu_init();
void ppu_powerup();

u8* ppu_get_pixel_buffer();

u8 ppu_get_redraw_flag();
void ppu_set_redraw_flag(u8 val);

u8 ppu_read_register(u8 reg_id);
void ppu_write_register(u8 reg_id, u8 value);

void ppu_tick();

void oam_dma_transfer_tick();

void ppu_cleanup();

#endif PPU_H