#pragma once

typedef void (*AudioCallback)(int num_samples, int num_channels, float* outs);
void audio_client_init();
void audio_client_open(int sample_rate, int buffer_size, int num_channels, AudioCallback callback);
void audio_client_close();
void audio_client_destroy();
