#pragma once

#ifndef MMU_H
#define MMU_H

#include "alu_binary.h"

u8 checksum_header;
u8 has_battery;


int mmu_init(u8* rom_buffer);
void mmu_powerup();
void mmu_cleanup();


u8 read(u16 addr);
void write(u16 addr, u8 value);


// Dumps the contents of the ERAM buffer into a file (.sav)
void save();


#endif MMU_H