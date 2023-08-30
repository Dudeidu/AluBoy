#pragma once

#ifndef CPU_H
#define CPU_H

#include "alu_binary.h"

int cpu_init();
void cpu_powerup();

u8 cpu_update();

u8  cpu_read_memory(u16 addr);
void cpu_write_memory(u16 addr, u8 value);

u8  cpu_read_register(u8 reg_id);
void cpu_write_register(u8 reg_id, u8 value);


#endif CPU_H