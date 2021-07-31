#ifndef video_reader_hpp
#define video_reader_hpp

#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

struct VideoReaderState {
    // Public properties to show
    bool reached_end;
    int width;
    int height;
    int frame_rate;
    int num_channels;
    int sample_rate;
    AVSampleFormat sample_format;
    AVRational video_time_base;
    AVRational audio_time_base;

    // Format internal state
    AVFormatContext* av_format_ctx;
    AVPacket* av_packet;

    // Video internal state
    AVCodecContext* video_codec_ctx;
    int video_stream_index;
    AVFrame* video_frame;
    SwsContext* sws_scaler_ctx;

    // Audio internal state
    AVCodecContext* audio_codec_ctx;
    int audio_stream_index;
    AVFrame* audio_frame;
};

constexpr int RECEIVED_VIDEO = -1;
constexpr int RECEIVED_NONE = 0;
// Positive values is the number of audio samples received

bool video_reader_open(VideoReaderState* state, const char* filename);
void video_reader_read_all_packets(VideoReaderState* state, std::function<void(bool is_video, bool is_keyframe, int pts, int dts, int duration)> visit_packet);
int  video_reader_next_frame(VideoReaderState* state, int* packet_pts, int* frame_pts);
void video_reader_transfer_video_frame(VideoReaderState* state, unsigned char* frame_buffer);
void video_reader_transfer_audio_frame(VideoReaderState* state, int size_1, float* buffer_1, int size_2, float* buffer_2);
bool video_reader_reached_end(VideoReaderState* state);
void video_reader_seek(VideoReaderState* state, bool video_pts, int pts);
void video_reader_close(VideoReaderState* state);

#endif
