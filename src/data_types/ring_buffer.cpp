#include "ring_buffer.hpp"
#include <cmath>
#include <assert.h>
#include <time.h>

constexpr double SAMPLE_RATE_GUESS = 44100.0;

// Constructor, destructor
void RingBuffer::init(RingBuffer* rb, int buffer_size) {
    rb->buffer_size = (int)exp2(ceil(log2(buffer_size)));
    rb->buffer = new float[rb->buffer_size];
    rb->write_point = 0;
    rb->read_point = 0;
}

void RingBuffer::destroy(RingBuffer* rb) {
    delete[] rb->buffer;
}

// Write functions
bool RingBuffer::can_write(int num_samples) {
    assert(num_samples <= this->buffer_size);

    int write_point = this->write_point.load();
    int read_point  = this->read_point.load();
    return (read_point + this->buffer_size - write_point >= num_samples);
}

void RingBuffer::write_start(int num_samples, int* size_1, float** buffer_1, int* size_2, float** buffer_2) {
    assert(num_samples <= this->buffer_size);

    int write_point = this->write_point.load();

    // Sleep until enough samples are available to write on
    int read_point;
    while (true) {
        read_point = this->read_point.load();
        auto available = (read_point + this->buffer_size) - write_point;
        if (available >= num_samples) {
            break;
        }

        auto remaining_in_seconds = (num_samples - available) / SAMPLE_RATE_GUESS;
        timespec ts;
        ts.tv_sec = (int)remaining_in_seconds;
        ts.tv_nsec = (int)((remaining_in_seconds - ts.tv_sec) * 1000000000.0);
        nanosleep(&ts, NULL);
    }

    int write_point_mod = write_point % this->buffer_size;
    if (write_point_mod + num_samples <= this->buffer_size) {
        *size_1 = num_samples;
        *buffer_1 = &this->buffer[write_point_mod];
        *size_2 = 0;
        *buffer_2 = NULL;
    } else {
        *size_1 = this->buffer_size - write_point_mod;
        *buffer_1 = &this->buffer[write_point_mod];
        *size_2 = num_samples - *size_1;
        *buffer_2 = &this->buffer[0];
    }
}

void RingBuffer::write_end(int num_samples) {
    this->write_point += num_samples;
}

// Read functions
bool RingBuffer::can_read(int num_samples) {
    assert(num_samples <= this->buffer_size);

    auto write_point = this->write_point.load();
    auto read_point  = this->read_point.load();
    return (read_point + num_samples <= write_point);
}

void RingBuffer::read_start(int num_samples, int* size_1, float** buffer_1, int* size_2, float** buffer_2) {
    assert(num_samples <= this->buffer_size);

    int read_point = this->read_point.load();

    // Sleep until enough samples are available to read
    int write_point;
    while (true) {
        write_point = this->write_point.load();
        auto available = write_point - read_point;
        if (available >= num_samples) {
            break;
        }

        auto remaining_in_seconds = (num_samples - available) / SAMPLE_RATE_GUESS;
        timespec ts;
        ts.tv_sec = (int)remaining_in_seconds;
        ts.tv_nsec = (int)((remaining_in_seconds - ts.tv_sec) * 1000000000.0);
        nanosleep(&ts, NULL);
    }

    int read_point_mod = read_point % this->buffer_size;
    if (read_point_mod + num_samples <= this->buffer_size) {
        *size_1 = num_samples;
        *buffer_1 = &this->buffer[read_point_mod];
        *size_2 = 0;
        *buffer_2 = NULL;
    } else {
        *size_1 = this->buffer_size - read_point_mod;
        *buffer_1 = &this->buffer[read_point_mod];
        *size_2 = num_samples - *size_1;
        *buffer_2 = &this->buffer[0];
    }
}

void RingBuffer::read_end(int num_samples) {
    assert(num_samples <= this->buffer_size);
    this->read_point += num_samples;
}
