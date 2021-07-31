#include <ddui/core>
#include <ddui/app>
#include <ddui/util/get_content_filename>
#include <ddui/views/ScrollArea>
#include <atomic>
#include <functional>
#include <algorithm>
#include <pthread.h>
#include "data_types/ring_buffer.hpp"
#include "video_reader.hpp"
#include "peak_image.hpp"
#include "audio_client.hpp"
#include <time.h>

constexpr int BUFFER_SIZE = 512;
constexpr int RING_BUFFER_SIZE = 8192;

static ScrollArea::ScrollAreaState scroll_area_state;
static VideoReaderState vr_state;
static float duration;
static RingBuffer rb;
int pkt_hovering;
static std::atomic_int pkt_requested;
static std::atomic_int pkt_playing;
static bool should_close;
static uint8_t* frame_buffer;
static std::atomic_bool frame_buffer_filled;
static pthread_t decode_thread;
static int image_id;

struct PacketInfo {
    enum PacketType : unsigned char {
        AUDIO,
        VIDEO_KEY,
        VIDEO_DELTA
    };

    PacketType type;
    int index;
    int pts;
    int dts;
    float duration;
    float time_start;
    float time_end;
};

static std::vector<PacketInfo> all_packets;
static std::vector<PacketInfo> video_packets;
static std::vector<PacketInfo> audio_packets;

static bool cmp_pkt_start(const PacketInfo& a, const PacketInfo& b) {
    return a.time_start < b.time_start;
}
static bool cmp_pkt_end(const PacketInfo& a, const PacketInfo& b) {
    return a.time_end < b.time_end;
}

static float draw_packets(std::vector<PacketInfo>* packets, float time_from, float time_to, float second_width, float y, int* next_pkt_hovering);

constexpr float FRAME_HEIGHT = 20;
constexpr float Y_SPACING = 10;
constexpr float PREVIEW_SCALE = 0.25;

static void open_file(const char* fname);
static void close_file();

void update() {
    auto ANIMATION_ID = (void*)0xF0;
    if (pkt_playing != -1 && !ddui::animation::is_animating(ANIMATION_ID)) {
        ddui::animation::start(ANIMATION_ID);
    }

    if (pkt_requested != -1 && !ddui::mouse_state.pressed) {
        pkt_requested = -1;
    }

    if (frame_buffer_filled) {
        ddui::update_image(image_id, frame_buffer);
        frame_buffer_filled = false;
    }
    
    if (ddui::has_dropped_files()) {
        close_file();
        open_file(ddui::file_drop_state.paths[0]);
        ddui::consume_dropped_files();
    }

    static float second_width = 512.0;
    if (ddui::has_key_event()) {
        if (ddui::key_state.character && ddui::key_state.character[0] == '-') {
            ddui::consume_key_event();
            second_width /= 2.0;
            if (second_width < 64.0) {
                second_width = 64.0;
            }
        }
        if (ddui::key_state.character && ddui::key_state.character[0] == '=') {
            ddui::consume_key_event();
            second_width *= 2.0;
        }
    }
    float area_width = duration * second_width;
    float view_width = ddui::view.width;
    ScrollArea::update(&scroll_area_state, area_width, ddui::view.height, [&]() {

        ddui::begin_path();
        ddui::fill_color(ddui::rgb(0x333333));
        ddui::rect(0, 0, ddui::view.width, ddui::view.height);
        ddui::fill();

        float time_from = scroll_area_state.scroll_x / second_width;
        float time_to   = (scroll_area_state.scroll_x + view_width) / second_width;
        int second_from = floor(time_from);
        int second_to   = ceil(time_to);

        // Draw second lines
        ddui::begin_path();
        ddui::stroke_width(1.0);
        ddui::stroke_color(ddui::rgb(0x555555));
        for (int s = second_from; s < second_to; ++s) {
            float second_x = s * second_width;
            ddui::move_to(second_x, 0);
            ddui::line_to(second_x, ddui::view.width);
        }
        ddui::stroke();

        float y = Y_SPACING;

        // Draw second numbers
        ddui::fill_color(ddui::rgb(0xffffff));
        ddui::font_face("mono");
        ddui::font_size(18.0);
        float asc, desc, lineh;
        ddui::text_metrics(&asc, &desc, &lineh);
        char second_str[16];
        for (int s = second_from; s < second_to; ++s) {
            sprintf(second_str, "%d", s);
            ddui::text(s * second_width + 4, y + asc, second_str, NULL);
        }
        y += lineh + Y_SPACING;

        int next_pkt_hovering = -1;

        // Draw video packets
        y = draw_packets(&video_packets, time_from, time_to, second_width, y, &next_pkt_hovering);

        // Draw audio packets
        y = draw_packets(&audio_packets, time_from, time_to, second_width, y, &next_pkt_hovering);

        // Draw mixed in-order packets
        y = draw_packets(&all_packets, time_from, time_to, second_width, y, &next_pkt_hovering);

        if (pkt_hovering != next_pkt_hovering) {
            pkt_hovering = next_pkt_hovering;
            ddui::repaint(NULL);
        }

    });

    if (image_id != -1) {
        ddui::save();
        ddui::translate(20, ddui::view.height - 20 - vr_state.height * PREVIEW_SCALE);
        auto paint = ddui::image_pattern(0,
                                         0,
                                         vr_state.width * PREVIEW_SCALE,
                                         vr_state.height * PREVIEW_SCALE,
                                         0,
                                         image_id,
                                         1.0f);
        ddui::fill_paint(paint);
        ddui::begin_path();
        ddui::rect(0, 0, vr_state.width * PREVIEW_SCALE, vr_state.height * PREVIEW_SCALE);
        ddui::fill();
        ddui::restore();
    }
}

float draw_packets(std::vector<PacketInfo>* packets, float time_from, float time_to, float second_width, float y, int* next_pkt_hovering) {

    // Lookup the visible packet range to draw
    int packet_from, packet_to;
    {
        PacketInfo pkt_lower_bound, pkt_upper_bound;
        pkt_lower_bound.time_end = time_from;
        pkt_upper_bound.time_start = time_to;

        auto it_from = std::lower_bound(packets->begin(), packets->end(), pkt_lower_bound, cmp_pkt_end);
        auto it_to   = std::upper_bound(it_from,          packets->end(), pkt_upper_bound, cmp_pkt_start);

        packet_from = it_from - packets->begin();
        packet_to   = it_to   - packets->begin();
    }

    ddui::stroke_width(1.0);
    for (int i = packet_from; i < packet_to; ++i) {
        auto& pkt = (*packets)[i];
        float pkt_x = pkt.time_start * second_width;
        float pkt_w = pkt.time_end   * second_width - pkt_x;
        float pkt_h = FRAME_HEIGHT;

        ddui::Color c;
        switch (pkt.type) {
            case PacketInfo::AUDIO:       c = ddui::rgb(0x33ff33); break;
            case PacketInfo::VIDEO_KEY:   c = ddui::rgb(0x3388ff); break;
            case PacketInfo::VIDEO_DELTA: c = ddui::rgb(0xff9922); break;
        }

        ddui::begin_path();
        ddui::rect(pkt_x, y, pkt_w, FRAME_HEIGHT);
        ddui::stroke_color(c);
        ddui::stroke();
        if (pkt.index == pkt_playing || (pkt_playing == -1 && pkt.index == pkt_hovering)) {
            ddui::fill_color(c);
            ddui::fill();
        }
        if (ddui::mouse_over(pkt_x, y, pkt_w, pkt_h)) {
            ddui::set_cursor(ddui::CURSOR_POINTING_HAND);
            *next_pkt_hovering = pkt.index;
        }
        if (ddui::mouse_hit(pkt_x, y, pkt_w, pkt_h)) {
            ddui::mouse_hit_accept();
            pkt_requested = pkt.index;
        }
    }

    y += FRAME_HEIGHT + Y_SPACING;

    return y;
}

void* decode_thread_func(void* ptr) {

    while (!should_close) {

        if (pkt_requested == -1) {
            pkt_playing = -1;
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 10000000;
            nanosleep(&ts, NULL);
            continue;
        }

        auto& pkt = all_packets[pkt_requested];
        video_reader_seek(&vr_state, pkt.type != PacketInfo::AUDIO, pkt.pts);

        if (pkt.type == PacketInfo::AUDIO) {
            while (pkt_requested != -1) {

                int res, packet_pts, pts;
                while ((res = video_reader_next_frame(&vr_state, &packet_pts, &pts)) == RECEIVED_VIDEO) {}

                if (res == RECEIVED_NONE) {
                    pkt_playing = -1;
                    pkt_requested = -1;
                    break;
                }

                auto it = std::find_if(all_packets.begin(), all_packets.end(), [&](PacketInfo& pkt) {
                    return pkt.type == PacketInfo::AUDIO && pkt.pts == pts;
                });
                if (it != all_packets.end()) {
                    pkt_playing = it - all_packets.begin();
                }

                int num_channels = vr_state.num_channels;

                int size_1, size_2;
                float *buffer_1, *buffer_2;
                rb.write_start(res * num_channels, &size_1, &buffer_1, &size_2, &buffer_2);
                video_reader_transfer_audio_frame(&vr_state, size_1 / num_channels, buffer_1, size_2 / num_channels, buffer_2);
                rb.write_end(res * num_channels);

            }
        } else {

            int res, packet_pts, pts;
            while ((res = video_reader_next_frame(&vr_state, &packet_pts, &pts)) != RECEIVED_NONE) {
                if (res != RECEIVED_VIDEO) {
                    continue;
                }

                // Find the packet we're looking at
                auto it = std::find_if(all_packets.begin(), all_packets.end(), [&](PacketInfo& pkt) {
                    return pkt.type != PacketInfo::AUDIO && pkt.pts == packet_pts;
                });
                if (it != all_packets.end()) {
                    pkt_playing = it - all_packets.begin();
                }

                if (pts == pkt.pts) {
                    break;
                }
            }

            if (res == RECEIVED_NONE) {
                pkt_playing = -1;
                pkt_requested = -1;
                break;
            }

            auto it = std::find_if(all_packets.begin(), all_packets.end(), [&](PacketInfo& pkt) {
                return pkt.type != PacketInfo::AUDIO && pkt.pts == pts;
            });
            if (it != all_packets.end()) {
                pkt_playing = it - all_packets.begin();
            }

            video_reader_transfer_video_frame(&vr_state, frame_buffer);
            frame_buffer_filled = true;
            pkt_requested = -1;
            pkt_playing = -1;
        }

    }

    return 0;
}

void audio_callback(int num_samples, int num_channels, float* buffer) {
    if (!rb.can_read(num_samples * num_channels)) {
        // Write silence
        auto ptr = buffer;
        auto ptr_end = buffer + num_samples * num_channels;
        while (ptr < ptr_end) {
            *ptr++ = 0.0;
        }
        return;
    }

    int size_1, size_2;
    float *buffer_1, *buffer_2;
    rb.read_start(num_samples * num_channels, &size_1, &buffer_1, &size_2, &buffer_2);
    memcpy(buffer, buffer_1, size_1 * sizeof(float));
    memcpy(buffer + size_1, buffer_2, size_2 * sizeof(float));
    rb.read_end(num_samples * num_channels);
}

void open_file(const char* fname) {

    should_close = false;
    pkt_requested = -1;
    pkt_playing = -1;
    pkt_hovering = -1;
    frame_buffer_filled = false;

    video_reader_open(&vr_state, fname);
    if (vr_state.video_stream_index != -1) {
        posix_memalign((void**)&frame_buffer, 128, vr_state.width * vr_state.height * 4);
        image_id = ddui::create_image_from_rgba(vr_state.width, vr_state.height, 0, frame_buffer);
    }

    if (vr_state.audio_stream_index != -1) {
        audio_client_open(vr_state.sample_rate, BUFFER_SIZE, vr_state.num_channels, audio_callback);
    }

    // Parse all packets
    duration = 0.0;
    video_reader_read_all_packets(&vr_state, [](bool is_video, bool is_keyframe, int pts, int dts, int dur) {
        auto time_base = is_video ? vr_state.video_time_base : vr_state.audio_time_base;

        PacketInfo pkt;
        pkt.type = is_video ? is_keyframe ? PacketInfo::VIDEO_KEY : PacketInfo::VIDEO_DELTA : PacketInfo::AUDIO;
        pkt.index = all_packets.size();
        pkt.pts = pts;
        pkt.dts = dts;
        pkt.duration = dur * (time_base.num / (float)time_base.den);

        if (is_video) {
            PacketInfo pkt_video = pkt;
            pkt_video.time_start = pts * (time_base.num / (float)time_base.den);
            pkt_video.time_end   = (pts + dur) * (time_base.num / (float)time_base.den);
            video_packets.push_back(pkt_video);
        } else {
            PacketInfo pkt_audio = pkt;
            pkt_audio.time_start = pts * (time_base.num / (float)time_base.den);
            pkt_audio.time_end   = (pts + dur) * (time_base.num / (float)time_base.den);
            audio_packets.push_back(pkt_audio);
        }

        all_packets.push_back(pkt);
        duration += pkt.duration;
    });

    int num_streams = audio_packets.empty() || video_packets.empty() ? 1 : 2;
    duration = duration / num_streams;
    float time = 0.0;
    for (int i = 0; i < all_packets.size(); ++i) {
        auto& pkt = all_packets[i];
        pkt.time_start = time;
        time += pkt.duration / num_streams;
        pkt.time_end   = time;
    }

    std::sort(video_packets.begin(), video_packets.end(), cmp_pkt_start);
    std::sort(audio_packets.begin(), audio_packets.end(), cmp_pkt_start);
    
    pthread_create(&decode_thread, NULL, decode_thread_func, NULL);
}

void close_file() {
    should_close = true;
    pthread_join(decode_thread, NULL);
    
    if (vr_state.audio_stream_index != -1) {
        audio_client_close();
    }
    if (vr_state.video_stream_index != -1) {
        free(frame_buffer);
        frame_buffer_filled = false;
        frame_buffer = NULL;
        ddui::delete_image(image_id);
        image_id = -1;
    }

    video_reader_close(&vr_state);

    should_close = false;
    pkt_requested = -1;
    pkt_playing = -1;
    pkt_hovering = -1;
    

    video_packets.clear();
    audio_packets.clear();
    all_packets.clear();
}

int main(int argc, const char** argv) {

    // ddui (graphics and UI system)
    if (!ddui::app_init(700, 600, "Video Inspector", update)) {
        printf("Failed to init ddui.\n");
        return 1;
    }

    // Type faces
    ddui::create_font("mono", "PTMono.ttf");

    RingBuffer::init(&rb, RING_BUFFER_SIZE);
    
    audio_client_init();

    // Open our video file
    auto fname = get_content_filename("demo.mp4");
    open_file(fname.c_str());

    ddui::app_run();

    close_file();
    audio_client_destroy();
    RingBuffer::destroy(&rb);

    return 0;
}
