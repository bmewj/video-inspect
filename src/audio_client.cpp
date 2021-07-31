#include "audio_client.hpp"

#include <portaudio.h>

#include <stdio.h>
#include <stdlib.h>

static PaStream *pa_stream;

static int buffer_size;
static int sample_rate;
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

int audio_client_init(int sample_rate_, int buffer_size_, int num_channels_, AudioCallback callback_) {
    sample_rate = sample_rate_;
    buffer_size = buffer_size_;
    num_channels = num_channels_;
    user_callback = callback_;

    char *stream_name = NULL;
    double latency = (double)buffer_size / sample_rate;

    PaError err;
    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    /* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream(
        &pa_stream,
        0,                     /* no input channels */
        num_channels,          /* no output channels */
        paFloat32,  /* 32 bit floating point output */
        sample_rate,
        buffer_size,
        pa_callback, /* this is your callback function */
        NULL); /* This is a pointer that will be passed to your callback*/
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    /* Start the stream */
    err = Pa_StartStream(pa_stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    return 0;
}

void audio_client_destroy() {
    PaError err;

    err = Pa_StopStream(pa_stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
    }
    Pa_Terminate();
}
