// ============================================================================
// globe.cpp — 地球旋转视频帧生成器
//
// 缩小椭圆0.966
// ============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>

constexpr double PI = 3.14159265358979323846;
constexpr double SHRINK = 0.966;

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> data;

    bool load(const char *fn) {
        int c;
        unsigned char *d = stbi_load(fn, &w, &h, &c, 3);
        if (!d) return false;
        data.assign(d, d + w * h * 3);
        stbi_image_free(d);
        return true;
    }

    bool save_png(const char *fn) {
        return stbi_write_png(fn, w, h, 3, data.data(), w * 3) != 0;
    }

    void create(int w_, int h_, unsigned char r = 0, unsigned char g = 0, unsigned char b = 0) {
        w = w_; h = h_;
        data.assign(w * h * 3, 0);
        for (size_t i = 0; i < data.size(); i += 3) {
            data[i] = r; data[i+1] = g; data[i+2] = b;
        }
    }

    unsigned char *pix(int x, int y) {
        if (x < 0) x = 0; if (x >= w) x = w - 1;
        if (y < 0) y = 0; if (y >= h) y = h - 1;
        return &data[(y * w + x) * 3];
    }

    const unsigned char *pix(int x, int y) const {
        if (x < 0) x = 0; if (x >= w) x = w - 1;
        if (y < 0) y = 0; if (y >= h) y = h - 1;
        return &data[(y * w + x) * 3];
    }

    void set_pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        auto p = &data[(y * w + x) * 3];
        p[0] = r; p[1] = g; p[2] = b;
    }
};

static void sample_bilinear(const Image &img, double sx, double sy,
                             unsigned char &r, unsigned char &g, unsigned char &b) {
    int x0 = (int)floor(sx);
    int y0 = (int)floor(sy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    double fx = sx - x0;
    double fy = sy - y0;

    auto p00 = img.pix(x0, y0);
    auto p10 = img.pix(x1, y0);
    auto p01 = img.pix(x0, y1);
    auto p11 = img.pix(x1, y1);

    double w00 = (1-fx)*(1-fy);
    double w10 = fx*(1-fy);
    double w01 = (1-fx)*fy;
    double w11 = fx*fy;

    r = (unsigned char)std::clamp(p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11, 0.0, 255.0);
    g = (unsigned char)std::clamp(p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11, 0.0, 255.0);
    b = (unsigned char)std::clamp(p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11, 0.0, 255.0);
}

static bool is_map_pixel(const unsigned char *p) {
    return std::max({p[0], p[1], p[2]}) > 8;
}

static void mollweide_forward(double lat, double lon, double &mx, double &my) {
    double theta = lat;
    for (int i = 0; i < 30; ++i) {
        double s2t = sin(2.0 * theta);
        double f = 2.0 * theta + s2t - PI * sin(lat);
        double df = 2.0 + 2.0 * cos(2.0 * theta);
        if (std::abs(df) < 1e-12) break;
        theta -= f / df;
    }
    theta = std::clamp(theta, -PI / 2.0, PI / 2.0);
    mx = lon / PI * cos(theta);
    my = sin(theta);
}

static void enhance_color(unsigned char &r, unsigned char &g, unsigned char &b) {
    int maxc = std::max({r, g, b});
    if (maxc < 2) return;
    
    double t = maxc / 255.0;
    double bright = 1.0 + 0.5 * (1.0 - t);
    
    double nr = r * bright;
    double ng = g * bright;
    double nb = b * bright;

    double gray = (nr + ng + nb) / 3.0;
    double sat = 1.3;
    nr = gray + (nr - gray) * sat;
    ng = gray + (ng - gray) * sat;
    nb = gray + (nb - gray) * sat;

    r = (unsigned char)std::clamp((int)nr, 0, 255);
    g = (unsigned char)std::clamp((int)ng, 0, 255);
    b = (unsigned char)std::clamp((int)nb, 0, 255);
}

static bool sample_ellipse(const Image &ell, double ecx, double ecy,
                            double rx, double ry,
                            double mx, double my,
                            unsigned char &r, unsigned char &g, unsigned char &b) {
    if (mx * mx + my * my > 1.0 + 1e-6) return false;
    double ex = ecx + mx * rx;
    double ey = ecy + my * ry;
    sample_bilinear(ell, ex, ey, r, g, b);
    enhance_color(r, g, b);
    return true;
}

static bool find_ellipse(const Image &img, double &cx, double &cy,
                          double &rx, double &ry) {
    double sx = 0, sy = 0;
    int cnt = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (is_map_pixel(img.pix(x, y))) {
                sx += x; sy += y; cnt++;
            }
    if (!cnt) return false;
    cx = sx / cnt;
    cy = sy / cnt;
    printf("  Center of mass: (%.1f, %.1f), count=%d\n", cx, cy, cnt);

    const int N = 720;
    double radii[N] = {0};
    int maxd = std::max(img.w, img.h);
    for (int a = 0; a < N; ++a) {
        double ang = a * 2 * PI / N;
        double dx = cos(ang), dy = sin(ang);
        bool in = false;
        for (int r = 1; r < maxd; ++r) {
            int x = (int)round(cx + r * dx);
            int y = (int)round(cy + r * dy);
            if (x < 0 || x >= img.w || y < 0 || y >= img.h) {
                radii[a] = r;
                break;
            }
            auto p = img.pix(x, y);
            int maxc = std::max({p[0], p[1], p[2]});
            if (maxc > 10)
                in = true;
            else if (in && maxc < 6) {
                radii[a] = r;
                break;
            }
        }
        if (radii[a] == 0) radii[a] = maxd;
    }

    double mnx = img.w, mxx = 0, mny = img.h, mxy = 0;
    for (int a = 0; a < N; ++a) {
        double ang = a * 2 * PI / N;
        double x = cx + radii[a] * cos(ang);
        double y = cy + radii[a] * sin(ang);
        if (x < mnx) mnx = x;
        if (x > mxx) mxx = x;
        if (y < mny) mny = y;
        if (y > mxy) mxy = y;
    }
    cx = (mnx + mxx) / 2;
    cy = (mny + mxy) / 2;
    rx = (mxx - mnx) / 2;
    ry = (mxy - mny) / 2;
    
    rx *= SHRINK;
    ry *= SHRINK;
    
    printf("  Ellipse: center=(%.1f,%.1f), rx=%.1f, ry=%.1f (shrunk)\n", cx, cy, rx, ry);
    return true;
}

static void extract_ellipse(const Image &src,
                             double cx, double cy, double rx, double ry,
                             Image &ell, double &ecx, double &ecy) {
    int ew = (int)ceil(2 * rx) + 3;
    int eh = (int)ceil(2 * ry) + 3;
    if (ew % 2 == 0) ew++;
    if (eh % 2 == 0) eh++;
    ell.create(ew, eh, 0, 0, 0);
    ecx = ew / 2.0;
    ecy = eh / 2.0;

    for (int ey = 0; ey < eh; ++ey)
        for (int ex = 0; ex < ew; ++ex) {
            double nx = (ex - ecx) / rx;
            double ny = (ey - ecy) / ry;
            if (nx * nx + ny * ny <= 1.0 + 1e-4) {
                unsigned char r, g, b;
                sample_bilinear(src, cx + nx * rx, cy + ny * ry, r, g, b);
                auto dp = ell.pix(ex, ey);
                dp[0] = r; dp[1] = g; dp[2] = b;
            }
        }

    printf("  Ellipse image: %d x %d\n", ew, eh);
}

static void generate_stars(Image &frame, int w, int h, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < 500; ++i) {
        int x = rand() % w;
        int y = rand() % h;
        int bright = 40 + rand() % 180;
        int size = 1 + rand() % 2;
        for (int dy = -size; dy <= size; ++dy)
            for (int dx = -size; dx <= size; ++dx) {
                int px = x + dx, py = y + dy;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    double dist = sqrt(dx*dx + dy*dy);
                    int b = (int)(bright * std::max(0.0, 1.0 - dist / (size + 1)));
                    auto p = frame.pix(px, py);
                    p[0] = std::min(255, p[0] + b);
                    p[1] = std::min(255, p[1] + b);
                    p[2] = std::min(255, p[2] + b);
                }
            }
    }
}

static void render_frame(Image &frame,
                          double earth_cx, double earth_cy, double earth_r,
                          double lon0, double lat0,
                          const Image &ell, double ecx, double ecy,
                          double rx, double ry) {
    int w = frame.w, h = frame.h;

    double Xx = -sin(lon0);
    double Xy = 0;
    double Xz = cos(lon0);

    double Yx = -sin(lat0) * cos(lon0);
    double Yy = cos(lat0);
    double Yz = -sin(lat0) * sin(lon0);

    double Zx = cos(lat0) * cos(lon0);
    double Zy = sin(lat0);
    double Zz = cos(lat0) * sin(lon0);

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double nx = (px - earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            double nz2 = 1.0 - nx * nx - ny * ny;
            if (nz2 < 0) continue;
            double nz = sqrt(nz2);

            double px3 = nx * Xx + ny * Yx + nz * Zx;
            double py3 = nx * Xy + ny * Yy + nz * Zy;
            double pz3 = nx * Xz + ny * Yz + nz * Zz;

            double lat = asin(std::clamp(py3, -1.0, 1.0));
            double lon = atan2(pz3, px3);

            double mx, my;
            mollweide_forward(lat, lon, mx, my);

            unsigned char r, g, b;
            if (sample_ellipse(ell, ecx, ecy, rx, ry, mx, my, r, g, b)) {
                double rim = 1.0 - nz;
                double glow = 1.0 + 0.2 * rim * rim;
                r = (unsigned char)std::clamp((int)(r * glow), 0, 255);
                g = (unsigned char)std::clamp((int)(g * glow), 0, 255);
                b = (unsigned char)std::clamp((int)(b * glow), 0, 255);
                frame.set_pixel(px, py, r, g, b);
            }
        }
    }

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double nx = (px - earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            double dist = sqrt(nx * nx + ny * ny);
            if (dist > 0.93 && dist < 1.07) {
                double alpha = 1.0 - std::abs(dist - 1.0) / 0.07;
                int glow = (int)(40 * alpha * alpha);
                auto p = frame.pix(px, py);
                p[0] = std::min(255, p[0] + glow);
                p[1] = std::min(255, p[1] + glow);
                p[2] = std::min(255, p[2] + glow + 40);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <输入图片> [帧数] [输出目录]\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    int num_frames = (argc > 2) ? atoi(argv[2]) : 360;
    const char *out_dir = (argc > 3) ? argv[3] : "frames";

    if (num_frames < 1) num_frames = 360;

    printf("=== 地球旋转视频帧生成器 ===\n");
    printf("输入: %s\n", input_file);
    printf("帧数: %d\n", num_frames);
    printf("输出目录: %s\n\n", out_dir);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", out_dir);
    system(cmd);

    Image src;
    if (!src.load(input_file)) {
        fprintf(stderr, "无法加载: %s\n", input_file);
        return 1;
    }
    printf("输入图片: %d x %d\n", src.w, src.h);

    double cx, cy, rx, ry;
    if (!find_ellipse(src, cx, cy, rx, ry)) {
        fprintf(stderr, "无法找到椭圆!\n");
        return 1;
    }

    Image ell;
    double ecx, ecy;
    extract_ellipse(src, cx, cy, rx, ry, ell, ecx, ecy);

    const int FW = 1920, FH = 1080;
    const double ER = 420.0;
    const double ECX = FW / 2.0;
    const double ECY = FH / 2.0;

    printf("\n输出: %dx%d, 地球半径=%.0fpx\n\n", FW, FH, ER);

    for (int f = 0; f < num_frames; ++f) {
        double lon0 = 2.0 * PI * f / num_frames;
        double lat0 = 0.0;

        Image frame;
        frame.create(FW, FH, 0, 0, 0);

        generate_stars(frame, FW, FH, 42 + f);
        render_frame(frame, ECX, ECY, ER, lon0, lat0,
                      ell, ecx, ecy, rx, ry);

        char fn[256];
        snprintf(fn, sizeof(fn), "%s/frame_%04d.png", out_dir, f);
        frame.save_png(fn);

        if (f % 30 == 0 || f == num_frames - 1)
            printf("  帧 %4d/%d\n", f + 1, num_frames);
    }

    printf("\n完成! 共 %d 帧 -> %s/\n", num_frames, out_dir);
    printf("合成视频:\n");
    printf("  ffmpeg -framerate 30 -i %s/frame_%%04d.png -c:v libx264 -pix_fmt yuv420p -crf 18 output.mp4\n", out_dir);

    return 0;
}
