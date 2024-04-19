#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define frand() (rand() / (float)RAND_MAX)

union Pixel {
    unsigned int rgba;
    struct {
        unsigned char r, g, b, a;
    };
};

class Image {
private:
    int w, h;
    Pixel* data;
public:
    Image(int w, int h) {
        this->w = w;
        this->h = h;
        data = (Pixel*)malloc(sizeof(Pixel) * w * h);
    }
    ~Image() {
        free(data);
    }
    int width () { return w; }
    int height() { return h; }
    const Pixel* pixels() { return data; }
    void setdata(const void* data) { memcpy(this->data, data, w * h * 4); }
    Pixel get(int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return {0};
        return data[y * w + x];
    }
    void set(int x, int y, Pixel pixel) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        data[y * w + x] = pixel;
    }
};

struct FloatingImage {
    float posX, posY;
    float velX, velY;
    float target_opacity, opac_inc, opacity;
    float scale_inc, scale;
};

Image* load_image(std::string path) {
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    Image* img = new Image(w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            img->set(x, y, *(Pixel*)(data + (y * w + x) * 4));
        }
    }
    stbi_image_free(data);
    return img;
}

#define UPSCALE 1.5f

int main(int argc, char** argv) {
    if (argc == 1) {
        printf("Usage:\nwaterfall-gif-gen path/to/file.png\n");
        return 1;
    }
    srand(clock());
    printf("Loading image\n");
    Image* base = load_image(argv[1]);
    Image* upscaled = new Image(base->width() * UPSCALE, base->height() * UPSCALE);
    for (int y = 0; y < base->height() * UPSCALE; y++) {
        for (int x = 0; x < base->width() * UPSCALE; x++) {
            int X = x / UPSCALE;
            int Y = y / UPSCALE;
            upscaled->set(x, y, base->get(X, Y));
        }
    }
    printf("Starting render...\n");
    std::vector<FloatingImage> images = {};
    FILE* ffmpeg = popen(("ffmpeg -hide_banner -loglevel error -y -r 15 -f rawvideo -pix_fmt rgba -s " + std::to_string(upscaled->width()) + "x" + std::to_string(upscaled->height()) + " -i - -vf \"scale=ceil((iw/ih*240)/2)*2:240\" -c:v h264 -pix_fmt yuv420p -b:v 96k video.mp4").c_str(), "w");
    for (int i = 0; i < 30; i++) {
        printf("\e[GRendering...%2d/90", i + 1);
        fflush(stdout);
        fwrite(upscaled->pixels(), upscaled->width() * upscaled->height() * 4, 1, ffmpeg);
    }
    for (int i = 0; i < 60; i++) {
        Image* frame = new Image(upscaled->width(), upscaled->height());
        frame->setdata(upscaled->pixels());
        for (int i = 0; i < 5; i++) {
            FloatingImage img;
            img.posX = upscaled->width () / 2.f;
            img.posY = upscaled->height() / 2.f;
            img.velX = (frand() * 4 - 2) * (base->width () / 50.f);
            img.velY = (frand() * 2 - 2) * (base->height() / 30.f);
            img.target_opacity = rand() % 101 / 100.f;
            img.opac_inc = img.target_opacity / (rand() % 15 + 10);
            img.opacity = 0;
            img.scale_inc = 1.f / (rand() % 5 + 40);
            img.scale = (rand() % 1) / 8.f;
            images.push_back(img);
        }
        int num_imgs = images.size();
        int curr_img = 0;
        for (auto& img : images) {
            printf("\e[GRendering...%2d/90 (%3d/%d)", i + 31, ++curr_img, num_imgs);
            fflush(stdout);
            img.posX += img.velX;
            img.posY += img.velY;
            img.velY += 0.5f;
            img.opacity += img.opac_inc;
            if (img.opacity >= img.target_opacity) img.opacity = img.target_opacity;
            img.scale += img.scale_inc;
            if (img.scale >= 1) img.scale = 1;
            for (int y = 0; y < base->height() * img.scale; y++) {
                for (int x = 0; x < base->width() * img.scale; x++) {
                    int X = x + img.posX - (base->width () * img.scale) / 2;
                    int Y = y + img.posY - (base->height() * img.scale) / 2;
                    Pixel floating = base->get(x / img.scale, y / img.scale);
                    Pixel curr = frame->get(X, Y);
                    curr.r = (floating.r - curr.r) * img.opacity + curr.r;
                    curr.g = (floating.g - curr.g) * img.opacity + curr.g;
                    curr.b = (floating.b - curr.b) * img.opacity + curr.b;
                    frame->set(X, Y, curr);
                }
            }
        }
        fwrite(frame->pixels(),frame->width() * frame->height() * 4, 1, ffmpeg);
        delete frame;
    }
    printf("\nFinishing up...\n");
    fflush(stdout);
    pclose(ffmpeg);
    printf("Generating palette...\n");
    fflush(stdout);
    system("ffmpeg -hide_banner -loglevel error -y -i video.mp4 -vf \"palettegen\" palette.png");
    printf("Generating gif...\n");
    fflush(stdout);
    system("ffmpeg -hide_banner -loglevel error -y -i video.mp4 -i palette.png -filter_complex \"paletteuse\" output.gif");
    printf("Cleaning up...\n");
    fflush(stdout);
    std::filesystem::remove("video.mp4");
    std::filesystem::remove("palette.png");
    delete base;
    delete upscaled;
    return 0;
}
