#pragma once

#ifndef APU_H
#define APU_H

#include "alu_binary.h"


void apu_init();
u8 apu_read_register(u8 reg_id);
int apu_write_register(u8 reg_id, u8 value);
void apu_frame_sequencer_update();

void apu_tick();

#endif APU_H