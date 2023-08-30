#pragma once

#ifndef TIMER_H
#define TIMER_H

#include "alu_binary.h"

u8  double_speed;
u16 internal_counter;   // the DIV register is the upper byte of this clock

void timer_init();
void timer_powerup();

u8 timer_read_register(u8 reg_id);
void timer_write_register(u8 reg_id, u8 value);

void timer_tick();


#endif TIMER_H