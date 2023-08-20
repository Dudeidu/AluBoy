#pragma once

#ifndef INPUT_H
#define INPUT_H

#include "alu_binary.h"

// get inputs from SDL
void input_update(u8* in);

// update P1 register & check for interrupts
void input_tick();

// load emulator with latest inputs
void input_joypad_update();

#endif INPUT_H