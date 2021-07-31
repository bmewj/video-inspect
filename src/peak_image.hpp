#ifndef peak_image_hpp
#define peak_image_hpp

#include <ddui/core>

struct Image {
    int image_id;
    int width;
    int height;
    unsigned char* data;
};

Image create_image(int width, int height);

void render_peak_image(Image img, float* buffer, int num_samples, int num_channels, ddui::Color color);

#endif
