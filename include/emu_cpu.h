#pragma once

#ifndef EMU_CPU_H
#define EMU_CPU_H

#include "alu_binary.h"

int emu_cpu_init(u8* rom_buffer);

void emu_cpu_update();

void emu_cpu_cleanup();

#endif EMU_CPU_H