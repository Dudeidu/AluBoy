#include "audio.h"
#include <math.h>
#include "macros.h"

#include <SDL.h>

const int AMPLITUDE = 28000;
const int SAMPLE_RATE = 44100;

SDL_AudioSpec want, have;
SDL_AudioDeviceID dev;

int sample_counter = 0;
unsigned short samples_gathered = 0;  // when we collect enough samples we output them to the audio device.
u8* sample_buffer = NULL;


int audio_init() {
 
    u8 debug_show_audio_devices = 0;
    if (debug_show_audio_devices) {
        int numDevices = SDL_GetNumAudioDevices(0); // 0 for playback devices
        for (int i = 0; i < numDevices; i++) {
            const char* deviceName = SDL_GetAudioDeviceName(i, 0); // 0 for playback devices
            printf("Playback Device %d: %s\n", i, deviceName);
        }
    }

    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 1028;
    want.callback = NULL;  // audio_callback
    //want.userdata = &sample_counter; // counter, keeping track of current sample number
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 0;
    }
    if (want.format != have.format) {
        fprintf(stderr, "Failed to initialize audio: could not get the desired AudioSpec\n");
        return 0;
    }

    sample_buffer = (u8*)malloc(sizeof(u8) * have.samples);

    SDL_PauseAudioDevice(dev, 0); // start playing sound

    return 1;
}

void audio_add_sample(u8 sample) {
    u8 sound_mode = 1;
    sample_buffer[samples_gathered++] = sample + have.silence;

    if (samples_gathered >= have.samples) {
        int success;

        //printf("samples in queue: %d\n", SDL_GetQueuedAudioSize(dev));
        // MODE 1 - Throttle audio generation: delay execution and the let queue drain to about a frame's worth
        // produces the most accurate sound, but might slow down performance.
        if (sound_mode == 0)
        {
            while ((SDL_GetQueuedAudioSize(dev)) > sizeof(u8) * have.samples) {
                SDL_Delay(1);
            }
        }
        // MODE 2 - Limit Enqueuing: only enqueue new samples if the queue has room for them
        // best performance, but could cause some audio skipping.
        // can adjust the size of the buffer that is allowed to be stores in queue
        else if (sound_mode == 1 && (SDL_GetQueuedAudioSize(dev)) > sizeof(u8) * have.samples * 4) {
            samples_gathered = 0;
            return;
        }
        // MODE 3 - Unlimited (unused) always queue new samples, even when the queue is full
        // good performance and no audio skipping, but might cause the audio to be delayed
        else {

        }

        success = SDL_QueueAudio(dev, sample_buffer, sizeof(u8) * samples_gathered);
        if (success != 0) {
            fprintf(stderr, "%s\n", SDL_GetError());
        }
        else {
            //printf("output sample size: %d\n", samples_gathered);
        }

        //printf("queued audio: %d\n",SDL_GetQueuedAudioSize(dev));
        samples_gathered = 0;
    }
}


void audio_cleanup() {
    if (sample_buffer) {
        free(sample_buffer);
    }
    SDL_CloseAudioDevice(dev);
}