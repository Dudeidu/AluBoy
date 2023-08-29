
#ifndef AUDIO_H
#define AUDIO_H

// Abstractions for SDL_audio stuff
#include <stdio.h>
#include <SDL_audio.h>

#include "alu_binary.h"

int audio_init();
void audio_cleanup();

/*
SDL_AudioSpec want, have;
SDL_AudioDeviceID dev;

SDL_memset(&want, 0, sizeof(want)); //or SDL_zero(want) 
want.freq = 48000;
want.format = AUDIO_F32;
want.channels = 2;
want.samples = 4096;
want.callback = MyAudioCallback;  // you wrote this function elsewhere.
dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
*/

#endif GRAPHICS_H