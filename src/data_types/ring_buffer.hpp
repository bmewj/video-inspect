#ifndef ring_buffer_hpp
#define ring_buffer_hpp

#include <atomic>

struct RingBuffer {
    float* buffer;
    int buffer_size;
    std::atomic_int write_point;
    std::atomic_int read_point;

    // Constructor, destructor
    static void init(RingBuffer* rb, int buffer_size);
    static void destroy(RingBuffer* rb);

    // Write functions
    bool can_write(int num_samples);
    void write_start(int num_samples, int* size_1, float** buffer_1, int* size_2, float** buffer_2);
    void write_end(int num_samples);

    // Read functions
    bool can_read(int num_samples);
    void read_start(int num_samples, int* size_1, float** buffer_1, int* size_2, float** buffer_2);
    void read_end(int num_samples);
};

#endif
