
#ifndef AUDIO_H
#define AUDIO_H

// Abstractions for SDL_audio stuff
#include <stdio.h>
#include <SDL_audio.h>

#include "alu_binary.h"

int audio_init();
void audio_cleanup();

void audio_add_sample(u8 sample);

#endif GRAPHICS_H