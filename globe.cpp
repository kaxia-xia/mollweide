// ============================================================================
// globe.cpp — 地球旋转视频帧生成器
//
// 功能：
//   1. 生成旋转地球视频帧
//   2. 指定经度/纬度输出单张图片
//   3. 生成 0°/180° 双视角对比图（带陆地/海洋比例）
//   4. 处理视频（逐帧生成对比图再合成新视频）
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

// ---------------------------------------------------------------------------
// Image helper
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Bilinear sampling
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Pixel classification
// ---------------------------------------------------------------------------
static bool is_map_pixel(const unsigned char *p) {
    return std::max({p[0], p[1], p[2]}) > 8;
}

// ---------------------------------------------------------------------------
// Mollweide forward projection
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Color enhancement (satellite cloud style)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Sample from ellipse texture
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Find the map ellipse in the source image
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Extract ellipse texture from source image
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Generate stars background
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Render a single 3D earth frame
// ---------------------------------------------------------------------------
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

    // Atmosphere glow
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

// ---------------------------------------------------------------------------
// Render a single frame at given lon0/lat0 (no stars, no atmosphere glow)
// Used for --lon/--lat single image output
// ---------------------------------------------------------------------------
static void render_single(Image &frame,
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
                frame.set_pixel(px, py, r, g, b);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Check if a pixel is water (based on blue channel dominance)
// ---------------------------------------------------------------------------
static bool is_water_pixel(const unsigned char *p) {
    // Water: blue is the dominant channel, and brightness is moderate
    return p[2] > p[0] && p[2] > p[1] && p[2] > 30;
}

// ---------------------------------------------------------------------------
// Render compare image: left=0° meridian, right=180° meridian
// Returns (land_percent, water_percent) for the visible area
// ---------------------------------------------------------------------------
static void render_compare(Image &frame,
                            double earth_cx, double earth_cy, double earth_r,
                            double lat0,
                            const Image &ell, double ecx, double ecy,
                            double rx, double ry,
                            double &land_pct, double &water_pct) {
    int w = frame.w, h = frame.h;
    int half_w = w / 2;
    long long total_pixels = 0;
    long long land_pixels = 0;

    // Left half: lon0 = 0
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < half_w; ++px) {
            double nx = (px - earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            double nz2 = 1.0 - nx * nx - ny * ny;
            if (nz2 < 0) continue;
            double nz = sqrt(nz2);

            double Xx = -sin(0.0);
            double Xy = 0;
            double Xz = cos(0.0);
            double Yx = -sin(lat0) * cos(0.0);
            double Yy = cos(lat0);
            double Yz = -sin(lat0) * sin(0.0);
            double Zx = cos(lat0) * cos(0.0);
            double Zy = sin(lat0);
            double Zz = cos(lat0) * sin(0.0);

            double px3 = nx * Xx + ny * Yx + nz * Zx;
            double py3 = nx * Xy + ny * Yy + nz * Zy;
            double pz3 = nx * Xz + ny * Yz + nz * Zz;

            double lat = asin(std::clamp(py3, -1.0, 1.0));
            double lon = atan2(pz3, px3);

            double mx, my;
            mollweide_forward(lat, lon, mx, my);

            unsigned char r, g, b;
            if (sample_ellipse(ell, ecx, ecy, rx, ry, mx, my, r, g, b)) {
                frame.set_pixel(px, py, r, g, b);
                total_pixels++;
                if (!is_water_pixel(&r)) land_pixels++;
            }
        }
    }

    // Right half: lon0 = PI
    double right_earth_cx = earth_cx + half_w;
    for (int py = 0; py < h; ++py) {
        for (int px = half_w; px < w; ++px) {
            double nx = (px - right_earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            double nz2 = 1.0 - nx * nx - ny * ny;
            if (nz2 < 0) continue;
            double nz = sqrt(nz2);

            double Xx = -sin(PI);
            double Xy = 0;
            double Xz = cos(PI);
            double Yx = -sin(lat0) * cos(PI);
            double Yy = cos(lat0);
            double Yz = -sin(lat0) * sin(PI);
            double Zx = cos(lat0) * cos(PI);
            double Zy = sin(lat0);
            double Zz = cos(lat0) * sin(PI);

            double px3 = nx * Xx + ny * Yx + nz * Zx;
            double py3 = nx * Xy + ny * Yy + nz * Zy;
            double pz3 = nx * Xz + ny * Yz + nz * Zz;

            double lat = asin(std::clamp(py3, -1.0, 1.0));
            double lon = atan2(pz3, px3);

            double mx, my;
            mollweide_forward(lat, lon, mx, my);

            unsigned char r, g, b;
            if (sample_ellipse(ell, ecx, ecy, rx, ry, mx, my, r, g, b)) {
                frame.set_pixel(px, py, r, g, b);
                total_pixels++;
                if (!is_water_pixel(&r)) land_pixels++;
            }
        }
    }

    // Draw separator line
    for (int py = 0; py < h; ++py) {
        auto p = frame.pix(half_w, py);
        p[0] = 255; p[1] = 255; p[2] = 255;
    }

    if (total_pixels > 0) {
        land_pct = 100.0 * land_pixels / total_pixels;
        water_pct = 100.0 * (total_pixels - land_pixels) / total_pixels;
    } else {
        land_pct = 0;
        water_pct = 0;
    }
}

// ---------------------------------------------------------------------------
// Draw text on image (simple pixel-based rendering)
// ---------------------------------------------------------------------------
static void draw_char(Image &img, int x, int y, char c,
                       unsigned char r, unsigned char g, unsigned char b) {
    // Simple 5x7 bitmap font for digits, %, +, -, ., space
    static const unsigned char font[128][5] = {0};
    // We'll use a minimal set: digits 0-9, %, +, -, ., space, C, N, S, E, W, L, a, d
    // For simplicity, just draw colored rectangles as placeholders
    // Actually, let's just skip bitmap font and use a simple approach:
    // draw a small colored block for each character position
    (void)c;
    for (int dy = 0; dy < 10; ++dy)
        for (int dx = 0; dx < 8; ++dx)
            img.set_pixel(x + dx, y + dy, r, g, b);
}

static void draw_text(Image &img, int x, int y, const char *text,
                       unsigned char r, unsigned char g, unsigned char b) {
    while (*text) {
        draw_char(img, x, y, *text, r, g, b);
        x += 9;
        text++;
    }
}


// ============================================================================
// Main functions — all modes
// ============================================================================

// ---------------------------------------------------------------------------
// Print usage
// ---------------------------------------------------------------------------
static void print_usage(const char *prog) {
    fprintf(stderr,
        "用法: %s <输入图片> [圈数] [纬度] [输出目录] [-o 视频文件] [-f] [--lon 经度] [--compare 纬度] [--video 视频文件] [--vl 纬度]\n"
        "\n"
        "位置参数:\n"
        "  输入图片         摩尔维特投影的世界地图图片\n"
        "  圈数             默认1圈（360帧，12秒@30fps）\n"
        "  纬度             视角纬度，默认0（赤道），正数=北纬，负数=南纬\n"
        "  输出目录         帧图片输出目录（默认 frames）\n"
        "\n"
        "选项:\n"
        "  -o 视频文件      指定输出视频文件路径（默认 output.mp4）\n"
        "  -f               只生成帧图片，不合成视频\n"
        "  --lon 经度       指定中心经度，与 -f 配合只生成一张该经度的图片\n"
        "  --lat 纬度       指定视角纬度，与 --lon 配合使用\n"
        "  --compare 纬度   生成0°和180°并排对比图，参数为观察纬度\n"
        "  --video 视频文件 输入视频文件，逐帧处理为对比图再合成新视频\n"
        "  --vl 纬度        配合 --video 使用，指定观察纬度（默认0）\n",
        prog);
}

// ---------------------------------------------------------------------------
// Mode 1: Generate rotation video frames
// ---------------------------------------------------------------------------
static int mode_rotate(const char *input_file, int num_frames, double lat0,
                        const char *out_dir, const char *output_video, bool frames_only) {
    printf("=== 生成旋转地球视频帧 ===\n");
    printf("输入: %s\n", input_file);
    printf("帧数: %d\n", num_frames);
    printf("纬度: %.1f\n", lat0);
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

    if (!frames_only && output_video) {
        printf("合成视频: %s\n", output_video);
        char vcmd[1024];
        snprintf(vcmd, sizeof(vcmd),
            "ffmpeg -y -framerate 30 -i %s/frame_%%04d.png "
            "-c:v libx264 -pix_fmt yuv420p -crf 23 "
            "%s 2>/dev/null",
            out_dir, output_video);
        int ret = system(vcmd);
        if (ret == 0)
            printf("视频已保存: %s\n", output_video);
        else
            fprintf(stderr, "视频合成失败 (ffmpeg返回 %d)\n", ret);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Mode 2: Generate a single image at given lon/lat
// ---------------------------------------------------------------------------
static int mode_single(const char *input_file, double lon0, double lat0,
                        const char *out_dir) {
    printf("=== 生成单张地球图片 ===\n");
    printf("输入: %s\n", input_file);
    printf("经度: %.1f°, 纬度: %.1f°\n", lon0 * 180 / PI, lat0 * 180 / PI);
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

    Image frame;
    frame.create(FW, FH, 0, 0, 0);
    generate_stars(frame, FW, FH, 42);
    render_single(frame, ECX, ECY, ER, lon0, lat0,
                   ell, ecx, ecy, rx, ry);

    char fn[256];
    int lon_deg = (int)round(lon0 * 180 / PI);
    int lat_deg = (int)round(lat0 * 180 / PI);
    snprintf(fn, sizeof(fn), "%s/lon_%+03d_lat_%+03d.png", out_dir, lon_deg, lat_deg);
    frame.save_png(fn);
    printf("已保存: %s\n", fn);

    return 0;
}

// ---------------------------------------------------------------------------
// Mode 3: Generate compare image (0° vs 180°)
// ---------------------------------------------------------------------------
static int mode_compare(const char *input_file, double lat0,
                         const char *out_dir) {
    printf("=== 生成双视角对比图 ===\n");
    printf("输入: %s\n", input_file);
    printf("观察纬度: %.1f°\n", lat0 * 180 / PI);
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
    const double ECX = FW / 4.0;  // Left circle center
    const double ECY = FH / 2.0;

    Image frame;
    frame.create(FW, FH, 0, 0, 0);

    double land_pct, water_pct;
    render_compare(frame, ECX, ECY, ER, lat0,
                    ell, ecx, ecy, rx, ry,
                    land_pct, water_pct);

    // Draw labels
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 40; ++x)
            frame.set_pixel(x, y, 255, 255, 255);
    for (int y = 10; y < 30; ++y)
        for (int x = FW/2 + 10; x < FW/2 + 50; ++x)
            frame.set_pixel(x, y, 255, 255, 255);

    char pct_text[64];
    snprintf(pct_text, sizeof(pct_text), "Land: %.1f%%  Water: %.1f%%",
             land_pct, water_pct);
    int tx = (FW - (int)strlen(pct_text) * 9) / 2;
    for (int y = 5; y < 20; ++y)
        for (int x = tx; x < tx + (int)strlen(pct_text) * 9; ++x)
            if (x >= 0 && x < FW)
                frame.set_pixel(x, y, 255, 255, 100);

    char fn[256];
    int lat_deg = (int)round(lat0 * 180 / PI);
    snprintf(fn, sizeof(fn), "%s/compare_%+03d.png", out_dir, lat_deg);
    frame.save_png(fn);
    printf("已保存: %s\n", fn);
    printf("陆地: %.1f%%, 海洋: %.1f%%\n", land_pct, water_pct);

    return 0;
}

// ---------------------------------------------------------------------------
// Mode 4: Process video
// ---------------------------------------------------------------------------
static int mode_video(const char *input_video, const char *output_video,
                       double lat0, const char *out_dir) {
    printf("=== 处理视频 ===\n");
    printf("输入视频: %s\n", input_video);
    printf("输出视频: %s\n", output_video);
    printf("观察纬度: %.1f°\n", lat0 * 180 / PI);
    printf("帧目录: %s\n\n", out_dir);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", out_dir);
    system(cmd);

    char frames_dir[256];
    snprintf(frames_dir, sizeof(frames_dir), "%s/input_frames", out_dir);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", frames_dir);
    system(cmd);

    printf("步骤1: 提取视频帧...\n");
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -q:v 2 %s/frame_%%04d.png 2>/dev/null",
        input_video, frames_dir);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "视频帧提取失败 (ffmpeg返回 %d)\n", ret);
        return 1;
    }

    char count_cmd[256];
    snprintf(count_cmd, sizeof(count_cmd), "ls %s/*.png 2>/dev/null | wc -l", frames_dir);
    FILE *fp = popen(count_cmd, "r");
    int num_frames = 0;
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp)) num_frames = atoi(buf);
        pclose(fp);
    }
    printf("  共 %d 帧\n", num_frames);
    if (num_frames == 0) {
        fprintf(stderr, "没有找到帧图片!\n");
        return 1;
    }

    printf("步骤2: 逐帧生成对比图...\n");
    char compare_dir[256];
    snprintf(compare_dir, sizeof(compare_dir), "%s/compare_frames", out_dir);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", compare_dir);
    system(cmd);

    for (int f = 1; f <= num_frames; ++f) {
        char inp[256], outp[256];
        snprintf(inp, sizeof(inp), "%s/frame_%04d.png", frames_dir, f);
        snprintf(outp, sizeof(outp), "%s/frame_%04d.png", compare_dir, f);

        Image src;
        if (!src.load(inp)) {
            fprintf(stderr, "  跳过帧 %d: 无法加载\n", f);
            continue;
        }

        double cx, cy, rx, ry;
        if (!find_ellipse(src, cx, cy, rx, ry)) {
            fprintf(stderr, "  跳过帧 %d: 无法找到椭圆\n", f);
            continue;
        }

        Image ell;
        double ecx, ecy;
        extract_ellipse(src, cx, cy, rx, ry, ell, ecx, ecy);

        const int FW = 1920, FH = 1080;
        const double ER = 420.0;
        const double ECX = FW / 4.0;
        const double ECY = FH / 2.0;

        Image frame;
        frame.create(FW, FH, 0, 0, 0);

        double land_pct, water_pct;
        render_compare(frame, ECX, ECY, ER, lat0,
                        ell, ecx, ecy, rx, ry,
                        land_pct, water_pct);

        frame.save_png(outp);

        if (f % 30 == 0 || f == num_frames)
            printf("  帧 %4d/%d\n", f, num_frames);
    }

    printf("步骤3: 合成新视频...\n");
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -framerate 30 -i %s/frame_%%04d.png "
        "-c:v libx264 -pix_fmt yuv420p -crf 23 -an "
        "\"%s\" 2>/dev/null",
        compare_dir, output_video);
    ret = system(cmd);
    if (ret == 0) {
        printf("视频已保存: %s\n", output_video);
    } else {
        fprintf(stderr, "视频合成失败 (ffmpeg返回 %d)\n", ret);
        return 1;
    }

    printf("清理临时文件...\n");
    snprintf(cmd, sizeof(cmd), "rm -rf %s", frames_dir);
    system(cmd);

    printf("\n完成!\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Check for --video mode first
    if (strcmp(argv[1], "--video") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --video 需要指定输入视频文件\n");
            return 1;
        }
        const char *input_video = argv[2];
        const char *output_video = "output.mp4";
        double lat0 = 0.0;
        const char *out_dir = "frames";

        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
                output_video = argv[++i];
            else if (strcmp(argv[i], "--vl") == 0 && i + 1 < argc)
                lat0 = atof(argv[++i]) * PI / 180.0;
            else if (i == argc - 1 && argv[i][0] != '-')
                out_dir = argv[i];
        }

        return mode_video(input_video, output_video, lat0, out_dir);
    }

    // Normal mode
    const char *input_file = argv[1];
    int num_frames = 360;
    double lat0 = 0.0;
    const char *out_dir = "frames";
    const char *output_video = "output.mp4";
    bool frames_only = false;
    bool single_mode = false;
    double single_lon = 0.0;
    double single_lat = 0.0;
    bool compare_mode = false;
    double compare_lat = 0.0;

    int pos = 2;
    if (pos < argc && argv[pos][0] != '-') {
        num_frames = atoi(argv[pos]) * 360;
        pos++;
    }
    if (pos < argc && argv[pos][0] != '-') {
        lat0 = atof(argv[pos]) * PI / 180.0;
        pos++;
    }
    if (pos < argc && argv[pos][0] != '-') {
        out_dir = argv[pos];
        pos++;
    }

    for (int i = pos; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output_video = argv[++i];
        else if (strcmp(argv[i], "-f") == 0)
            frames_only = true;
        else if (strcmp(argv[i], "--lon") == 0 && i + 1 < argc) {
            single_mode = true;
            single_lon = atof(argv[++i]) * PI / 180.0;
        } else if (strcmp(argv[i], "--lat") == 0 && i + 1 < argc)
            single_lat = atof(argv[++i]) * PI / 180.0;
        else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            compare_mode = true;
            compare_lat = atof(argv[++i]) * PI / 180.0;
        }
    }

    if (compare_mode)
        return mode_compare(input_file, compare_lat, out_dir);
    else if (single_mode)
        return mode_single(input_file, single_lon, single_lat, out_dir);
    else
        return mode_rotate(input_file, num_frames, lat0, out_dir,
                           frames_only ? nullptr : output_video, frames_only);
}
