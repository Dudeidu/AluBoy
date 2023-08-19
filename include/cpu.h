#pragma once

#ifndef CPU_H
#define CPU_H

#include "alu_binary.h"

int cpu_init(u8* rom_buffer);

void cpu_update(u8* inputs);

void cpu_cleanup();

#endif CPU_H