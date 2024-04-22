#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

#include "opt.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define frand() (rand() / (float)RAND_MAX)

#ifdef WINDOWS
#define POPEN_FLAGS "wb"
#define RETURN_TO_LINE_START "\r"
#else
#define POPEN_FLAGS "w"
#define RETURN_TO_LINE_START "\e[G"
#endif

#define STR(x) #x

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

std::string ifilename = "";
std::string ofilename = "output.gif";

struct {
#define SETTING(type, name, value) type name = value;
#include "settings.h"
#undef SETTING
} settings;

std::string replace(std::string in) {
    std::replace(in.begin(), in.end(), '_', '-');
    return in;
}

void calculate_scaled_width_and_height(int basew, int baseh, int* w, int* h) {
    *w = settings.max_height * ((float)basew / baseh);
    *h = settings.max_height;
    if (*w > settings.max_width) {
        *w = settings.max_width;
        *h = settings.max_width * ((float)baseh / basew);
    }
    *w = floor(*w / 2.f) * 2.f;
    *h = floor(*h / 2.f) * 2.f;
}

bool print_help(struct OptIter* iter = nullptr) {
    printf("Usage:\nwaterfall-gif-gen [options] -i,--input file.png -o,--output file.gif\n");
    printf("Valid options are:\n");
#define SETTING(type, name, value) printf("   --%-20s Default: " STR(value) "\n", replace(STR(name)).c_str());
#include "settings.h"
#undef SETTING
    printf("-h,--help                 Print this message\n");
    printf("-i,--input                Specify input file\n");
    printf("-o,--output               Specify output file\n");
    if (iter) exit(0);
    return true;
}

bool input_file(struct OptIter* iter) {
    ifilename = opt_get(iter);
    return true;
}

bool output_file(struct OptIter* iter) {
    ofilename = opt_get(iter);
    return true;
}

bool invalid_opt(struct OptIter* iter) {
    printf("Invalid option: %s\n", opt_get(iter));
    exit(1);
    return false;
}

#define ARG value
#define PARSER_FUNC(type, parser) \
type type ## _parser(const char* value) { \
    return parser; \
}

PARSER_FUNC(int,   std::stoi(ARG))
PARSER_FUNC(float, std::stof(ARG))

#undef GET_ARG
#undef PARSER_FUNC

void parse_opt(int argc, char** argv) {
    struct OptList* opt = opt_create();
#define SETTING(type, name, value) opt_add(opt, replace(STR(name)).c_str(), NOCHAR, [](struct OptIter* iter) -> bool { settings.name = type ## _parser(opt_get(iter)); return true; });
#include "settings.h"
#undef SETTING
    opt_add(opt, "help"  , 'h',  print_help);
    opt_add(opt, "input" , 'i',  input_file);
    opt_add(opt, "output", 'o', output_file);
    opt_invalid(opt, invalid_opt);
    opt_run(argc, argv, opt);
}

float get_rand(float min, float max) {
    if (min == max) return min;
    return frand() * (max - min) + min;
}

int get_rand_int(int min, int max) {
    return rand() % (max - min + 1) + min;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        print_help();
        return 1;
    }
    parse_opt(argc, argv);
    if (ifilename.empty()) {
        printf("Missing input file\n");
        return 1;
    }
    srand(settings.seed == 0 ? clock() : settings.seed);
    printf("Loading image\n");
    Image* base = load_image(ifilename.c_str());
    int scaled_width, scaled_height;
    calculate_scaled_width_and_height(base->width(), base->height(), &scaled_width, &scaled_height);
    Image* scaled = new Image(scaled_width, scaled_height);
    for (int y = 0; y < scaled->height(); y++) {
        for (int x = 0; x < scaled->width(); x++) {
            int X = x / ((float)scaled->width () / base->width ());
            int Y = y / ((float)scaled->height() / base->height());
            scaled->set(x, y, base->get(X, Y));
        }
    }
    printf("Starting render...\n");
    std::vector<FloatingImage> images = {};
    FILE* ffmpeg = popen(("ffmpeg -hide_banner -loglevel error -y -r " + std::to_string(settings.framerate) + " -f rawvideo -pix_fmt rgba -s " + std::to_string(scaled->width()) + "x" + std::to_string(scaled->height()) + " -i - -c:v h264 -pix_fmt yuv420p -b:v 96k video.mp4").c_str(), POPEN_FLAGS);
    for (int i = 0; i < settings.delay; i++) {
        printf(RETURN_TO_LINE_START "Rendering...%2d/%d", i + 1, settings.delay + settings.waterfall_frames);
        fflush(stdout);
        fwrite(scaled->pixels(), scaled->width() * scaled->height() * 4, 1, ffmpeg);
    }
    for (int i = 0; i < settings.waterfall_frames; i++) {
        Image* frame = new Image(scaled->width(), scaled->height());
        frame->setdata(scaled->pixels());
        int generate_imgs = get_rand_int(settings.min_img_per_frame, settings.max_img_per_frame);
        for (int j = 0; j < generate_imgs && i < settings.img_gen_frames; j++) {
            FloatingImage img;
            img.posX = get_rand(settings.min_spawn_x, settings.max_spawn_x);
            img.posY = get_rand(settings.min_spawn_y, settings.max_spawn_y);
            img.velX = get_rand(settings.min_horiz_speed, settings.max_horiz_speed);
            img.velY = get_rand(settings.min_vert_speed, settings.max_vert_speed);
            img.target_opacity = get_rand(settings.min_opacity, settings.max_opacity);
            img.opac_inc = img.target_opacity / get_rand_int(settings.min_opacity_inc, settings.max_opacity_inc);
            img.opacity = 0;
            img.scale_inc = 1.f / get_rand_int(settings.min_scale_inc, settings.max_scale_inc);
            img.scale = get_rand(settings.min_init_scale, settings.min_init_scale);
            images.push_back(img);
        }
        int num_imgs = images.size();
        int curr_img = 0;
        for (auto& img : images) {
            printf(RETURN_TO_LINE_START "Rendering...%2d/%d (%3d/%d)", i + settings.delay + 1, settings.delay + settings.waterfall_frames, ++curr_img, num_imgs);
            fflush(stdout);
            img.posX += img.velX;
            img.posY += img.velY;
            img.velY += settings.gravity;
            img.opacity += img.opac_inc;
            if (img.opacity >= img.target_opacity) img.opacity = img.target_opacity;
            img.scale += img.scale_inc;
            if (img.scale >= 1) img.scale = 1;
            int imgw = scaled->width () * img.scale * settings.scale_multiplier;
            int imgh = scaled->height() * img.scale * settings.scale_multiplier;
            for (int y = 0; y < imgh; y++) {
                for (int x = 0; x < imgw; x++) {
                    int X = x + img.posX * frame->width () - imgw / 2.f;
                    int Y = y + img.posY * frame->height() - imgh / 2.f;
                    Pixel floating = base->get(x / (float)imgw * base->width(), y / (float)imgh * base->height());
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
    delete scaled;
    return 0;
}
