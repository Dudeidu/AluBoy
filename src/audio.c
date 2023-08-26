#include "audio.h"
#include <math.h>
#include "macros.h"

#include <SDL.h>

#define TAU 6.2831855

const int AMPLITUDE = 28000;
const int SAMPLE_RATE = 44100;

SDL_AudioSpec want, have;
SDL_AudioDeviceID dev;

int sample_counter = 0;

// Forward declarations
void audio_callback(void* userdata, u8* stream, int len);
void beep(int duration);

int audio_init() {
 
    int numDevices = SDL_GetNumAudioDevices(0); // 0 for playback devices
    for (int i = 0; i < numDevices; i++) {
        const char* deviceName = SDL_GetAudioDeviceName(i, 0); // 0 for playback devices
        printf("Playback Device %d: %s\n", i, deviceName);
    }

    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;  // you wrote this function elsewhere.
    want.userdata = &sample_counter; // counter, keeping track of current sample number
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 0;
    }
    if (want.format != have.format) {
        fprintf(stderr, "Failed to initialize audio: could not get the desired AudioSpec\n");
        return 0;
    }

    beep(200);

    return 1;
}

// Callback function
void audio_callback(void* userdata, u8* stream, int len) {
    u8* buffer      = (u8*)stream;
    int length      = len;
    int* counter    = (int*)userdata;
    double volume   = 0.10; // Adjust this value to control the volume
    int silence     = have.silence;

    for(int i = 0; i < length; i++, (*counter)++)
    {
        double time = (double)(*counter) / (double)SAMPLE_RATE;
        double sample = 0.5 * sin(TAU * 441.0 * time) + 0.5; // Map -1~1 to 0~1

        // converts sine wave to square wave (slow, just for testing)
        if (sample > 0.5) sample = 1.0;
        else sample = 0.0;

        sample *= volume;
        //fprintf(stdout, "%d ", (u8)(sample * 256));
        //fflush(stdout);
        // 
        // Convert the double sample to u8 using your silence point of 128
        buffer[i] = (u8)(sample * silence); // 256 because 256 * 0.5 = 128
    }
}


void beep(int duration) {
    SDL_PauseAudioDevice(dev, 0); // start playing sound
    SDL_Delay(duration); // wait while sound is playing
    SDL_PauseAudioDevice(dev, 1); // stop playing sound
}


void audio_cleanup() {
    printf("sample counter: %d\n", sample_counter);
    SDL_CloseAudioDevice(dev);
}