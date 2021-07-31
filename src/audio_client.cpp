#include "audio_client.hpp"

#include <portaudio.h>

#include <stdio.h>
#include <stdlib.h>

static PaStream *pa_stream;

static int num_channels;
static AudioCallback user_callback;

static int pa_callback(const void *inputBuffer, void *outputBuffer,
                       unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags,
                       void *userData) {
    user_callback(framesPerBuffer, num_channels, (float*)outputBuffer);
    return 0;
}

void audio_client_init() {
    PaError err;
    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        exit(1);
    }
}

void audio_client_open(int sample_rate, int buffer_size, int num_channels_, AudioCallback callback) {
    PaError err;

    num_channels = num_channels_;
    user_callback = callback;

    err = Pa_OpenDefaultStream(&pa_stream, 0, num_channels, paFloat32, sample_rate, buffer_size, pa_callback, NULL);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        exit(1);
    }

    err = Pa_StartStream(pa_stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        exit(1);
    }
}

void audio_client_close() {
    PaError err;
    err = Pa_StopStream(pa_stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        exit(1);
    }
}

void audio_client_destroy() {
    Pa_Terminate();
}
