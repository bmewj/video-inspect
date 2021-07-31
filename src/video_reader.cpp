#include "video_reader.hpp"
#include <assert.h>
#include <pthread.h>

// av_err2str returns a temporary array. This doesn't work in gcc.
// This function can be used as a replacement for av_err2str.
static const char* av_make_error(int errnum) {
    static char str[AV_ERROR_MAX_STRING_SIZE];
    memset(str, 0, sizeof(str));
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}

static AVPixelFormat correct_for_deprecated_pixel_format(AVPixelFormat pix_fmt) {
    // Fix swscaler deprecated pixel format warning
    // (YUVJ has been deprecated, change pixel format to regular YUV)
    switch (pix_fmt) {
        case AV_PIX_FMT_YUVJ420P: return AV_PIX_FMT_YUV420P;
        case AV_PIX_FMT_YUVJ422P: return AV_PIX_FMT_YUV422P;
        case AV_PIX_FMT_YUVJ444P: return AV_PIX_FMT_YUV444P;
        case AV_PIX_FMT_YUVJ440P: return AV_PIX_FMT_YUV440P;
        default:                  return pix_fmt;
    }
}

inline static float convert_sample(int32_t sample) {
    return (float)sample / -INT32_MIN;
}

bool video_reader_open(VideoReaderState* state, const char* filename) {

    state->reached_end = false;

    // Open the file using libavformat
    AVFormatContext* av_format_ctx = state->av_format_ctx = avformat_alloc_context();
    if (!av_format_ctx) {
        printf("Couldn't created AVFormatContext\n");
        return false;
    }

    if (avformat_open_input(&av_format_ctx, filename, NULL, NULL) != 0) {
        printf("Couldn't open audio file\n");
        return false;
    }

    // Find the first valid audio stream inside the file
    state->video_stream_index = -1;
    state->audio_stream_index = -1;
    AVCodecParameters* video_codec_params;
    AVCodecParameters* audio_codec_params;
    AVCodec* video_codec = NULL;
    AVCodec* audio_codec = NULL;
    for (int i = 0; i < av_format_ctx->nb_streams; ++i) {
        AVStream* av_stream = av_format_ctx->streams[i];
        AVCodecParameters* av_codec_params = av_stream->codecpar;
        AVCodec* av_codec = avcodec_find_decoder(av_codec_params->codec_id);
        if (!av_codec) {
            continue;
        }
        if (!video_codec && av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_codec_params = av_codec_params;
            video_codec = av_codec;
            state->video_stream_index = i;
            state->width = av_codec_params->width;
            state->height = av_codec_params->height;
            state->frame_rate = av_stream->avg_frame_rate.num;
            assert(av_stream->avg_frame_rate.den == 1);
            state->video_time_base = av_stream->time_base;
            continue;
        }
        if (!audio_codec && av_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_codec_params = av_codec_params;
            audio_codec = av_codec;
            state->num_channels = audio_codec_params->channels;
            state->sample_rate = audio_codec_params->sample_rate;
            state->audio_stream_index = i;
            state->audio_time_base = av_stream->time_base;
            continue;
        }
    }
    if (!audio_codec && !video_codec) {
        printf("Couldn't find valid audio or video stream inside file\n");
        return false;
    }

    // Set up a video codec context for the decoder
    if (video_codec) {
        AVCodecContext* ctx = state->video_codec_ctx = avcodec_alloc_context3(video_codec);
        if (!ctx) {
            printf("Couldn't create AVCodecContext\n");
            return false;
        }
        if (avcodec_parameters_to_context(ctx, video_codec_params) < 0) {
            printf("Couldn't initialize AVCodecContext\n");
            return false;
        }
        if (avcodec_open2(ctx, video_codec, NULL) < 0) {
            printf("Couldn't open codec\n");
            return false;
        }

        state->video_frame = av_frame_alloc();
        if (!state->video_frame) {
            printf("Couldn't allocate AVFrame\n");
            return false;
        }
    } else {
        state->video_codec_ctx = NULL;
        state->video_frame = NULL;
    }

    // Set up a audio codec context for the decoder
    if (audio_codec) {
        AVCodecContext* ctx = state->audio_codec_ctx = avcodec_alloc_context3(audio_codec);
        if (!ctx) {
            printf("Couldn't create AVCodecContext\n");
            return false;
        }
        if (avcodec_parameters_to_context(ctx, audio_codec_params) < 0) {
            printf("Couldn't initialize AVCodecContext\n");
            return false;
        }
        if (avcodec_open2(ctx, audio_codec, NULL) < 0) {
            printf("Couldn't open codec\n");
            return false;
        }
        state->sample_format = ctx->sample_fmt;

        state->audio_frame = av_frame_alloc();
        if (!state->audio_frame) {
            printf("Couldn't allocate AVFrame\n");
            return false;
        }
    } else {
        state->audio_codec_ctx = NULL;
        state->audio_frame = NULL;
    }

    state->av_packet = av_packet_alloc();
    if (!state->av_packet) {
        printf("Couldn't allocate AVPacket\n");
        return false;
    }

    state->sws_scaler_ctx = NULL;

    return true;
}

void video_reader_read_all_packets(VideoReaderState* state, std::function<void(bool is_video, bool is_keyframe, int pts, int dts, int duration)> visit_packet) {
    int response;
    while (true) {
        response = av_read_frame(state->av_format_ctx, state->av_packet);
        if (response == AVERROR_EOF) {
            state->reached_end = true;
            break;
        } else if (response < 0) {
            printf("Failed to read frame: %s\n", av_make_error(response));
            break;
        }

        if (state->av_packet->stream_index == state->video_stream_index) {
            visit_packet(true,
                         (state->av_packet->flags & AV_PKT_FLAG_KEY),
                         state->av_packet->pts,
                         state->av_packet->dts,
                         state->av_packet->duration);
        } else if (state->av_packet->stream_index == state->audio_stream_index) {
            visit_packet(false,
                         true,
                         state->av_packet->pts,
                         state->av_packet->dts,
                         state->av_packet->duration);
        }

        av_packet_unref(state->av_packet);
    }
}

int video_reader_next_frame(VideoReaderState* state, int* pts) {

    // Decode one frame
    int response;
    while (true) {
        response = av_read_frame(state->av_format_ctx, state->av_packet);
        if (response == AVERROR_EOF) {
            state->reached_end = true;
            return RECEIVED_NONE;
        } else if (response < 0) {
            printf("Failed to read frame: %s\n", av_make_error(response));
            return RECEIVED_NONE;
        }

        if (state->av_packet->stream_index == state->video_stream_index) {

            response = avcodec_send_packet(state->video_codec_ctx, state->av_packet);
            if (response < 0) {
                printf("Failed to decode packet: %s\n", av_make_error(response));
                return RECEIVED_NONE;
            }

            response = avcodec_receive_frame(state->video_codec_ctx, state->video_frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                av_packet_unref(state->av_packet);
                continue;
            } else if (response < 0) {
                printf("Failed to decode packet: %s\n", av_make_error(response));
                return RECEIVED_NONE;
            }

            *pts = state->av_packet->pts;
            av_packet_unref(state->av_packet);
            return RECEIVED_VIDEO;

        } else if (state->av_packet->stream_index == state->audio_stream_index) {

            response = avcodec_send_packet(state->audio_codec_ctx, state->av_packet);
            if (response < 0) {
                printf("Failed to decode packet: %s\n", av_make_error(response));
                return RECEIVED_NONE;
            }

            response = avcodec_receive_frame(state->audio_codec_ctx, state->audio_frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                av_packet_unref(state->av_packet);
                continue;
            } else if (response < 0) {
                printf("Failed to decode packet: %s\n", av_make_error(response));
                return RECEIVED_NONE;
            }

            *pts = state->av_packet->pts;
            av_packet_unref(state->av_packet);
            state->num_channels = state->audio_frame->channels;
            state->sample_rate  = state->audio_frame->sample_rate;
            return state->audio_frame->nb_samples;

        } else {
            av_packet_unref(state->av_packet);
            continue;
        }

    }

    return RECEIVED_NONE;
}

void video_reader_transfer_video_frame(VideoReaderState* state, unsigned char* frame_buffer) {
    
    // Set up sws scaler
    if (!state->sws_scaler_ctx) {
        auto source_pix_fmt = correct_for_deprecated_pixel_format(state->video_codec_ctx->pix_fmt);
        state->sws_scaler_ctx = sws_getContext(state->width, state->height, source_pix_fmt,
                                               state->width, state->height, AV_PIX_FMT_RGB0,
                                               SWS_BILINEAR, NULL, NULL, NULL);
    }
    if (!state->sws_scaler_ctx) {
        printf("Couldn't initialize sw scaler\n");
        assert(0);
    }

    uint8_t* dest[4] = { frame_buffer, NULL, NULL, NULL };
    int dest_linesize[4] = { state->width * 4, 0, 0, 0 };
    sws_scale(state->sws_scaler_ctx,
              state->video_frame->data,
              state->video_frame->linesize,
              0,
              state->video_frame->height,
              dest,
              dest_linesize);

}

static void video_reader_copy_audio_buffer(VideoReaderState* state, int offset, int size, float* buffer) {

    auto frame = state->audio_frame;

    int num_channels = state->num_channels;

    if (state->sample_format == AV_SAMPLE_FMT_S32) {

        int32_t* ptr_in     = (int32_t*)frame->data[0] + offset * num_channels;
        int32_t* ptr_in_end = ptr_in + size * num_channels;
        float*   ptr_out    = buffer;
        while (ptr_in < ptr_in_end) {
            *ptr_out++ = convert_sample(*ptr_in++);
        }

    } else if (state->sample_format == AV_SAMPLE_FMT_FLTP) {

        float* ptr_ins[num_channels];
        for (int i = 0; i < num_channels; ++i) {
            ptr_ins[i] = (float*)frame->data[i] + offset;
        }
        int samples = size * num_channels;
        for (int i = 0; i < samples; ++i) {
            int channel = i % num_channels;
            buffer[i] = *ptr_ins[channel]++;
        }
    
    } else if (state->sample_format == AV_SAMPLE_FMT_FLT) {

        memcpy(buffer,
               (float*)frame->data[0] + offset * num_channels,
               sizeof(float) * size * num_channels);

    } else {
        assert(0);
    }
}

void video_reader_transfer_audio_frame(VideoReaderState* state, int size_1, float* buffer_1, int size_2, float* buffer_2) {
    assert(size_1 + size_2 == state->audio_frame->nb_samples);
    video_reader_copy_audio_buffer(state, 0, size_1, buffer_1);
    if (size_2 > 0) {
        video_reader_copy_audio_buffer(state, size_1, size_2, buffer_2);
    }
}

bool video_reader_reached_end(VideoReaderState* state) {
    return state->reached_end;
}

void video_reader_seek(VideoReaderState* state, bool video_pts, int pts) {
    av_seek_frame(state->av_format_ctx, video_pts ? state->video_stream_index : state->audio_stream_index, pts, AVSEEK_FLAG_BACKWARD);
    if (state->audio_codec_ctx) {
        avcodec_flush_buffers(state->audio_codec_ctx);
    }
    if (state->video_codec_ctx) {
        avcodec_flush_buffers(state->video_codec_ctx);
    }
}

void video_reader_close(VideoReaderState* state) {
    avformat_close_input(&state->av_format_ctx);
    avformat_free_context(state->av_format_ctx);
    if (state->video_frame) {
        av_frame_free(&state->video_frame);
    }
    if (state->audio_frame) {
        av_frame_free(&state->audio_frame);
    }
    if (state->sws_scaler_ctx) {
        sws_freeContext(state->sws_scaler_ctx);
    }
    av_packet_free(&state->av_packet);
    if (state->video_codec_ctx) {
        avcodec_free_context(&state->video_codec_ctx);
    }
    if (state->audio_codec_ctx) {
        avcodec_free_context(&state->audio_codec_ctx);
    }
}
