#pragma once

typedef void (*AudioCallback)(int num_samples, int num_channels, float* outs);
int audio_client_init(int sample_rate, int buffer_size, int num_channels, AudioCallback callback);
void audio_client_destroy();

