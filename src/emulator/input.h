#pragma once

#ifndef INPUT_H
#define INPUT_H

#include "alu_binary.h"

u8 input_updated; // whether the inputs were already fetched this frame

// get inputs from SDL
void input_update(u8* in);

void input_tick();

void input_joypad_update();

#endif INPUT_H