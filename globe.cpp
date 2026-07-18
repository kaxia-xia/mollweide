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
bool purple_mode = false;

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
static bool is_water_pixel(const unsigned char *p);

// ---------------------------------------------------------------------------
// Mollweide forward projection — optimized with fewer iterations
// ---------------------------------------------------------------------------
static void mollweide_forward(double lat, double lon, double &mx, double &my) {
    double theta = lat;
    for (int i = 0; i < 4; ++i) {
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
// Purple glow fantasy mode — land becomes glowing purple, ocean dark purple
// ---------------------------------------------------------------------------
static void apply_purple_glow(unsigned char &r, unsigned char &g, unsigned char &b, bool is_water) {
    int maxc = std::max({r, g, b});
    if (maxc < 2) return;
    double bright = maxc / 255.0;
    if (is_water) {
        r = (unsigned char)std::clamp((int)(25 + bright * 55), 0, 255);
        g = (unsigned char)std::clamp((int)(5 + bright * 20), 0, 255);
        b = (unsigned char)std::clamp((int)(45 + bright * 75), 0, 255);
    } else {
        r = (unsigned char)std::clamp((int)(120 + bright * 110), 0, 255);
        g = (unsigned char)std::clamp((int)(15 + bright * 30), 0, 255);
        b = (unsigned char)std::clamp((int)(160 + bright * 95), 0, 255);
        if (bright > 0.35) {
            int extra = (int)((bright - 0.35) * 1.5 * 80);
            r = std::min(255, r + extra);
            b = std::min(255, b + extra);
        }
    }
}


// ---------------------------------------------------------------------------
// Sample from ellipse texture
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Improved ellipse sampling with seamless horizontal wrapping
// Samples from both sides near the ±180° meridian and blends for smooth transition
// ---------------------------------------------------------------------------
static bool sample_ellipse(const Image &ell, double ecx, double ecy,
                            double rx, double ry,
                            double mx, double my,
                            unsigned char &r, unsigned char &g, unsigned char &b) {
    if (mx * mx + my * my > 1.0 + 1e-5) return false;

    // Normalize mx to [-1, 1] range with wrapping
    double wmx = mx;
    while (wmx > 1.0) wmx -= 2.0;
    while (wmx < -1.0) wmx += 2.0;

    // Compute wrapped mx (shift by ±2 to get the other side of the texture)
    double wmx2 = (wmx >= 0.0) ? wmx - 2.0 : wmx + 2.0;

    // When |wmx| > 0.92, we are near the ±180° meridian (texture edge).
    // Both primary and wrapped samples may be affected by dark boundary pixels.
    // Use an inset sample (slightly inside the texture) for both and blend.
    double wrap_blend = std::max(0.0, (std::abs(wmx) - 0.92) / 0.08);

    // Primary sample (with inset to avoid dark edge pixels)
    double inset = 1.0 - 0.01 * wrap_blend;  // up to 1% inset at the edge
    double wmx_inset = wmx * inset;
    double ex = ecx + wmx_inset * rx;
    double ey = ecy + my * ry;
    sample_bilinear(ell, ex, ey, r, g, b);

    // Wrapped sample (also with inset)
    double wmx2_inset = wmx2 * inset;
    double ex2 = ecx + wmx2_inset * rx;
    unsigned char r2 = 0, g2 = 0, b2 = 0;
    sample_bilinear(ell, ex2, ey, r2, g2, b2);

    bool primary_valid = (r > 8 || g > 8 || b > 8);
    bool wrapped_valid = (r2 > 8 || g2 > 8 || b2 > 8);

    if (wrap_blend > 0.0 && wrapped_valid) {
        // Near the edge: blend towards wrapped sample
        r = (unsigned char)(r * (1.0 - wrap_blend) + r2 * wrap_blend);
        g = (unsigned char)(g * (1.0 - wrap_blend) + g2 * wrap_blend);
        b = (unsigned char)(b * (1.0 - wrap_blend) + b2 * wrap_blend);
        return true;
    }

    // Use primary if valid
    if (primary_valid) return true;

    // Use wrapped if primary invalid
    if (wrapped_valid) {
        r = r2; g = g2; b = b2;
        return true;
    }

    // If neither is valid, try sampling further inside
    double inset2 = 0.98;
    double wmx_inset2 = wmx * inset2;
    double ex_inset2 = ecx + wmx_inset2 * rx;
    sample_bilinear(ell, ex_inset2, ey, r, g, b);
    if (r > 8 || g > 8 || b > 8) return true;

    // Try even further inside (progressively deeper insets)
    for (double d = 0.96; d >= 0.80; d -= 0.04) {
        double wmx_inset3 = wmx * d;
        double ex_inset3 = ecx + wmx_inset3 * rx;
        sample_bilinear(ell, ex_inset3, ey, r, g, b);
        if (r > 8 || g > 8 || b > 8) return true;
        
        // Also try wrapped with this inset
        double wmx2_inset3 = wmx2 * d;
        double ex2_inset3 = ecx + wmx2_inset3 * rx;
        sample_bilinear(ell, ex2_inset3, ey, r2, g2, b2);
        if (r2 > 8 || g2 > 8 || b2 > 8) {
            r = r2; g = g2; b = b2;
            return true;
        }
    }

    return false;
}

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
    // Make texture wider (4*rx) so that wrapped sampling (mx ± 2.0) stays in bounds.
    // mx=-1..+1 maps to ex = ecx ± rx, and mx±2 maps to ex = ecx ± 3*rx.
    // With ew=4*rx and ecx=ew/2, mx=±1 maps to ex=ecx±rx (within texture),
    // and mx=±1 wrapped by ±2 maps to ex=ecx∓rx (also within texture).
    int ew = (int)ceil(4 * rx) + 3;
    int eh = (int)ceil(2 * ry) + 3;
    if (ew % 2 == 0) ew++;
    if (eh % 2 == 0) eh++;
    ell.create(ew, eh, 0, 0, 0);
    ecx = ew / 2.0;
    ecy = eh / 2.0;

    // First pass: sample all pixels within the ellipse from the source
    for (int ey = 0; ey < eh; ++ey)
        for (int ex = 0; ex < ew; ++ex) {
            double nx = (ex - ecx) / rx;
            double ny = (ey - ecy) / ry;
            if (nx * nx + ny * ny <= 1.0 + 5e-3) {
                unsigned char r, g, b;
                sample_bilinear(src, cx + nx * rx, cy + ny * ry, r, g, b);
                auto dp = ell.pix(ex, ey);
                dp[0] = r; dp[1] = g; dp[2] = b;
            }
        }

    // Second pass: fill pixels outside the ellipse (but within the texture bounds)
    // by wrapping horizontally from the opposite side.
    // In Mollweide projection, mx=-1 (left edge) and mx=+1 (right edge) represent
    // the same meridian (180°). So the texture should wrap seamlessly:
    // left padding  ← right edge content
    // right padding ← left edge content
    for (int ey = 0; ey < eh; ++ey) {
        int first = -1, last = -1;
        for (int ex = 0; ex < ew; ++ex) {
            auto dp = ell.pix(ex, ey);
            if (dp[0] != 0 || dp[1] != 0 || dp[2] != 0) {
                if (first < 0) first = ex;
                last = ex;
            }
        }
        if (first < 0) continue;
        
        // Fill left padding from right side (wrapping: nx -> nx + 2.0)
        if (first > 0) {
            for (int ex = 0; ex < first; ++ex) {
                double nx = (ex - ecx) / rx;
                double wrapped_nx = nx + 2.0;
                double wrapped_ex = ecx + wrapped_nx * rx;
                if (wrapped_ex >= 0 && wrapped_ex < ew) {
                    auto sp = ell.pix((int)round(wrapped_ex), ey);
                    if (sp[0] != 0 || sp[1] != 0 || sp[2] != 0) {
                        auto dp = ell.pix(ex, ey);
                        dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                    }
                }
            }
        }
        // Fill right padding from left side (wrapping: nx -> nx - 2.0)
        if (last < ew - 1) {
            for (int ex = last + 1; ex < ew; ++ex) {
                double nx = (ex - ecx) / rx;
                double wrapped_nx = nx - 2.0;
                double wrapped_ex = ecx + wrapped_nx * rx;
                if (wrapped_ex >= 0 && wrapped_ex < ew) {
                    auto sp = ell.pix((int)round(wrapped_ex), ey);
                    if (sp[0] != 0 || sp[1] != 0 || sp[2] != 0) {
                        auto dp = ell.pix(ex, ey);
                        dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                    }
                }
            }
        }
    }

    // Third pass: fix dark/anti-aliased pixels at the left/right edges of the ellipse.
    // Only repair pixels that are suspiciously dark (likely anti-aliasing artifacts
    // at the ellipse boundary), by sampling from slightly further inside the SAME side.
    // Do NOT cross-replace between left and right edges, as that would break maps
    // where the 180° meridian passes through continents (e.g. Rodinia supercontinent).
    for (int ey = 0; ey < eh; ++ey) {
        int first = -1, last = -1;
        for (int ex = 0; ex < ew; ++ex) {
            auto dp = ell.pix(ex, ey);
            if (dp[0] != 0 || dp[1] != 0 || dp[2] != 0) {
                if (first < 0) first = ex;
                last = ex;
            }
        }
        if (first < 0 || last - first < 6) continue;
        
        // Fix dark pixels at left edge by sampling from further right on same side
        for (int i = 0; i < 3 && first + i < ew; ++i) {
            auto dp = ell.pix(first + i, ey);
            int maxc = std::max({dp[0], dp[1], dp[2]});
            if (maxc < 25) {
                // Sample from 4 pixels further inside (same side)
                int src_ex = first + i + 4;
                if (src_ex < ew) {
                    auto sp = ell.pix(src_ex, ey);
                    int sp_max = std::max({sp[0], sp[1], sp[2]});
                    if (sp_max > 30) {
                        dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                    }
                }
            }
        }
        // Fix dark pixels at right edge by sampling from further left on same side
        for (int i = 0; i < 3 && last - i >= 0; ++i) {
            auto dp = ell.pix(last - i, ey);
            int maxc = std::max({dp[0], dp[1], dp[2]});
            if (maxc < 25) {
                int src_ex = last - i - 4;
                if (src_ex >= 0) {
                    auto sp = ell.pix(src_ex, ey);
                    int sp_max = std::max({sp[0], sp[1], sp[2]});
                    if (sp_max > 30) {
                        dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                    }
                }
            }
        }
    }

    // Fourth pass: for any remaining pure black pixels inside the ellipse,
    // fill by wrapping horizontally.
    for (int ey = 0; ey < eh; ++ey) {
        for (int ex = 0; ex < ew; ++ex) {
            auto dp = ell.pix(ex, ey);
            int maxc = std::max({dp[0], dp[1], dp[2]});
            if (maxc < 8) {
                double nx = (ex - ecx) / rx;
                double ny = (ey - ecy) / ry;
                if (nx * nx + ny * ny <= 1.0) {
                    double wrapped_nx = (nx > 0.0) ? nx - 2.0 : nx + 2.0;
                    double wrapped_ex = ecx + wrapped_nx * rx;
                    if (wrapped_ex >= 0 && wrapped_ex < ew) {
                        auto sp = ell.pix((int)round(wrapped_ex), ey);
                        int sp_max = std::max({sp[0], sp[1], sp[2]});
                        if (sp_max > 25) {
                            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                        }
                    }
                }
            }
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
// Post-processing: repair seam artifacts (isolated dark pixels surrounded by
// non-dark pixels). These occur at the 180° meridian where the texture edge
// may have dark pixels due to the source ellipse boundary.
// Only repairs pixels that are within the earth sphere (not space background).
// ---------------------------------------------------------------------------
static void repair_seam(Image &frame, double earth_cx, double earth_cy, double earth_r) {
    int w = frame.w, h = frame.h;
    for (int py = 1; py < h - 1; ++py) {
        for (int px = 1; px < w - 1; ++px) {
            // Skip pixels outside the earth sphere
            double nx = (px - earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            if (nx * nx + ny * ny > 1.0) continue;

            auto dp = frame.pix(px, py);
            int maxc = std::max({dp[0], dp[1], dp[2]});
            // Only repair pixels that are dark (likely seam artifacts)
            if (maxc < 25) {
                int bright_neighbors = 0;
                int avg_r = 0, avg_g = 0, avg_b = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        auto np = frame.pix(px + dx, py + dy);
                        int np_maxc = std::max({np[0], np[1], np[2]});
                        if (np_maxc > 30) {
                            bright_neighbors++;
                            avg_r += np[0];
                            avg_g += np[1];
                            avg_b += np[2];
                        }
                    }
                }
                // Require at least 4 bright neighbors to avoid filling
                // legitimate dark pixels at the edge of the earth sphere
                if (bright_neighbors >= 4) {
                    dp[0] = avg_r / bright_neighbors;
                    dp[1] = avg_g / bright_neighbors;
                    dp[2] = avg_b / bright_neighbors;
                }
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
                if (purple_mode) {
                    unsigned char pix[3] = {r, g, b};
                    apply_purple_glow(r, g, b, is_water_pixel(pix));
                }
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

    // Repair seam artifacts
    repair_seam(frame, earth_cx, earth_cy, earth_r);
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
                if (purple_mode) {
                    unsigned char pix[3] = {r, g, b};
                    apply_purple_glow(r, g, b, is_water_pixel(pix));
                }
                frame.set_pixel(px, py, r, g, b);
            }
        }
    }

    // Repair seam artifacts
    repair_seam(frame, earth_cx, earth_cy, earth_r);
}

static bool is_water_pixel(const unsigned char *p) {
    int r = p[0], g = p[1], b = p[2];
    int maxc = std::max({r, g, b});
    if (maxc < 3) return true;

    // Key insight: in true ocean, BLUE is significantly higher than BOTH
    // red and green. Land pixels may have blue as the max channel too,
    // but the difference is smaller, especially between blue and green.

    // 1) Deep ocean: blue >> red, blue > green, significant margins
    if (b > r && b > g && b > 40 && (b - r) > 20 && (b - g) > 8 && r < 110 && g < 130) return true;

    // 2) Shallow/coastal: blue >= green > red, red very low
    if (b >= g && g > r && r < 65 && g > 25 && b > 25 && (b - r) > 28 && (b - g) > 5) return true;

    // 3) Very dark: all channels very low, blue slightly dominant
    if (maxc < 30 && b >= r && b >= g && r < 20 && g < 20) return true;

    // 4) Bright white/cyan over water: blue clearly dominates
    if (r > 240 && g > 240 && b > 240 && b > r && b > g) return true;

    return false;
}

// ---------------------------------------------------------------------------
// Count land/water ratio from the original Mollweide map (source image)
// This is more accurate than counting from the rendered 3D view.
// ---------------------------------------------------------------------------
static void count_land_water_original(const Image &src,
                                       double cx, double cy, double rx, double ry,
                                       double &land_pct, double &water_pct) {
    long long total_pixels = 0;
    long long land_pixels = 0;

    // Iterate over all pixels in the source image
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            // Check if pixel is within the map ellipse
            double nx = (x - cx) / rx;
            double ny = (y - cy) / ry;
            if (nx * nx + ny * ny > 1.0) continue; // outside map

            auto p = src.pix(x, y);
            int maxc = std::max({p[0], p[1], p[2]});
            if (maxc < 3) continue; // black background inside ellipse? skip

            total_pixels++;
            if (!is_water_pixel(p)) {
                land_pixels++;
            }
        }
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
static void render_compare(Image &frame,
                            double earth_cx, double earth_cy, double earth_r,
                            double lat0,
                            const Image &ell, double ecx, double ecy,
                            double rx, double ry,
                            double &land_pct, double &water_pct) {
    int w = frame.w, h = frame.h;
    int half_w = w / 2;
    // Left half: lon0 = 0 — precompute rotation matrix
    double Xx_l = -sin(0.0), Xy_l = 0, Xz_l = cos(0.0);
    double Yx_l = -sin(lat0) * cos(0.0), Yy_l = cos(lat0), Yz_l = -sin(lat0) * sin(0.0);
    double Zx_l = cos(lat0) * cos(0.0), Zy_l = sin(lat0), Zz_l = cos(lat0) * sin(0.0);

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < half_w; ++px) {
            double nx = (px - earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            double nz2 = 1.0 - nx * nx - ny * ny;
            if (nz2 < 0) continue;
            double nz = sqrt(nz2);

            double px3 = nx * Xx_l + ny * Yx_l + nz * Zx_l;
            double py3 = nx * Xy_l + ny * Yy_l + nz * Zy_l;
            double pz3 = nx * Xz_l + ny * Yz_l + nz * Zz_l;

            double lat = asin(std::clamp(py3, -1.0, 1.0));
            double lon = atan2(pz3, px3);

            double mx, my;
            mollweide_forward(lat, lon, mx, my);

            unsigned char r, g, b;
            if (sample_ellipse(ell, ecx, ecy, rx, ry, mx, my, r, g, b)) {
                unsigned char pix[3] = {r, g, b};
                bool water = is_water_pixel(pix);
                if (purple_mode) apply_purple_glow(r, g, b, water);
                frame.set_pixel(px, py, r, g, b);
            }
        }

    }
    // Right half: lon0 = PI
    double right_earth_cx = earth_cx + half_w;
    // Right half: lon0 = PI — precompute rotation matrix
    double Xx_r = -sin(PI), Xy_r = 0, Xz_r = cos(PI);
    double Yx_r = -sin(lat0) * cos(PI), Yy_r = cos(lat0), Yz_r = -sin(lat0) * sin(PI);
    double Zx_r = cos(lat0) * cos(PI), Zy_r = sin(lat0), Zz_r = cos(lat0) * sin(PI);

    for (int py = 0; py < h; ++py) {
        for (int px = half_w; px < w; ++px) {
            double nx = (px - right_earth_cx) / earth_r;
            double ny = (py - earth_cy) / earth_r;
            double nz2 = 1.0 - nx * nx - ny * ny;
            if (nz2 < 0) continue;
            double nz = sqrt(nz2);

            double px3 = nx * Xx_r + ny * Yx_r + nz * Zx_r;
            double py3 = nx * Xy_r + ny * Yy_r + nz * Zy_r;
            double pz3 = nx * Xz_r + ny * Yz_r + nz * Zz_r;

            double lat = asin(std::clamp(py3, -1.0, 1.0));
            double lon = atan2(pz3, px3);

            double mx, my;
            mollweide_forward(lat, lon, mx, my);
            unsigned char r, g, b;
            if (sample_ellipse(ell, ecx, ecy, rx, ry, mx, my, r, g, b)) {
                unsigned char pix[3] = {r, g, b};
                bool water = is_water_pixel(pix);
                if (purple_mode) apply_purple_glow(r, g, b, water);
                frame.set_pixel(px, py, r, g, b);
            }
        }
    }


    // Draw separator line
    for (int py = 0; py < h; ++py) {
        auto p = frame.pix(half_w, py);
        p[0] = 255; p[1] = 255; p[2] = 255;
    }

    // Post-processing: repair seam artifacts for both earth spheres
    repair_seam(frame, earth_cx, earth_cy, earth_r);
    repair_seam(frame, earth_cx + half_w, earth_cy, earth_r);

    // land_pct and water_pct are now set by count_land_water_original() before calling this function
    // They remain unchanged from the caller-provided values.
}

// ---------------------------------------------------------------------------

// 8x8 bitmap font (ASCII 32-127)
// 8x8 bitmap font for ASCII 32-127 (index 0 = space)
static const unsigned char font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, // 33 !
    {0x6c,0x6c,0x6c,0x00,0x00,0x00,0x00,0x00}, // 34 "
    {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00}, // 35 #
    {0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00}, // 36 $
    {0x00,0x66,0xac,0x18,0x34,0x6a,0x0c,0x00}, // 37 %
    {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, // 38 &
    {0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, // 39 '
    {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, // 40 (
    {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00}, // 41 )
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, // 42 *
    {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00}, // 43 +
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ,
    {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, // 45 -
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 .
    {0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00}, // 47 /
    {0x3c,0x66,0x6e,0x7e,0x76,0x66,0x3c,0x00}, // 48 0
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, // 49 1
    {0x3c,0x66,0x06,0x0c,0x18,0x30,0x7e,0x00}, // 50 2
    {0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00}, // 51 3
    {0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0x00}, // 52 4
    {0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00}, // 53 5
    {0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00}, // 54 6
    {0x7e,0x06,0x0c,0x18,0x30,0x30,0x30,0x00}, // 55 7
    {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00}, // 56 8
    {0x3c,0x66,0x66,0x3e,0x06,0x66,0x3c,0x00}, // 57 9
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // 58 :
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // 59 ;
    {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00}, // 60 <
    {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, // 61 =
    {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00}, // 62 >
    {0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00}, // 63 ?
    {0x3c,0x66,0x6e,0x6e,0x6e,0x60,0x3c,0x00}, // 64 @
    {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00}, // 65 A
    {0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00}, // 66 B
    {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00}, // 67 C
    {0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00}, // 68 D
    {0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00}, // 69 E
    {0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00}, // 70 F
    {0x3c,0x66,0x60,0x6e,0x66,0x66,0x3e,0x00}, // 71 G
    {0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00}, // 72 H
    {0x7e,0x18,0x18,0x18,0x18,0x18,0x7e,0x00}, // 73 I
    {0x06,0x06,0x06,0x06,0x06,0x66,0x3c,0x00}, // 74 J
    {0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00}, // 75 K
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00}, // 76 L
    {0x66,0x76,0x7e,0x7e,0x66,0x66,0x66,0x00}, // 77 M
    {0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0x00}, // 78 N
    {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // 79 O
    {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00}, // 80 P
    {0x3c,0x66,0x66,0x66,0x6e,0x3c,0x06,0x00}, // 81 Q
    {0x7c,0x66,0x66,0x7c,0x78,0x6c,0x66,0x00}, // 82 R
    {0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00}, // 83 S
    {0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 84 T
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // 85 U
    {0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00}, // 86 V
    {0x66,0x66,0x66,0x7e,0x7e,0x76,0x66,0x00}, // 87 W
    {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00}, // 88 X
    {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00}, // 89 Y
    {0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00}, // 90 Z
    {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00}, // 91 [
    {0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00}, // 92 backslash
    {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00}, // 93 ]
    {0x18,0x3c,0x66,0x00,0x00,0x00,0x00,0x00}, // 94 ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff}, // 95 _
    {0x18,0x0c,0x06,0x00,0x00,0x00,0x00,0x00}, // 96 \`
    {0x00,0x00,0x3c,0x06,0x3e,0x66,0x3e,0x00}, // 97 a
    {0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00}, // 98 b
    {0x00,0x00,0x3c,0x66,0x60,0x66,0x3c,0x00}, // 99 c
    {0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0x00}, // 100 d
    {0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00}, // 101 e
    {0x1c,0x30,0x7c,0x30,0x30,0x30,0x30,0x00}, // 102 f
    {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x3c}, // 103 g
    {0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x00}, // 104 h
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00}, // 105 i
    {0x06,0x00,0x06,0x06,0x06,0x06,0x66,0x3c}, // 106 j
    {0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00}, // 107 k
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // 108 l
    {0x00,0x00,0x6c,0x7e,0x7e,0x66,0x66,0x00}, // 109 m
    {0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x00}, // 110 n
    {0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00}, // 111 o
    {0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0x60}, // 112 p
    {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x06}, // 113 q
    {0x00,0x00,0x7c,0x66,0x60,0x60,0x60,0x00}, // 114 r
    {0x00,0x00,0x3c,0x60,0x3c,0x06,0x7c,0x00}, // 115 s
    {0x30,0x30,0x7c,0x30,0x30,0x30,0x1c,0x00}, // 116 t
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3e,0x00}, // 117 u
    {0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00}, // 118 v
    {0x00,0x00,0x66,0x66,0x7e,0x7e,0x3c,0x00}, // 119 w
    {0x00,0x00,0x66,0x3c,0x18,0x3c,0x66,0x00}, // 120 x
    {0x00,0x00,0x66,0x66,0x66,0x3e,0x06,0x3c}, // 121 y
    {0x00,0x00,0x7e,0x0c,0x18,0x30,0x7e,0x00}, // 122 z
    {0x0c,0x18,0x18,0x30,0x18,0x18,0x0c,0x00}, // 123 {
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 124 |
    {0x30,0x18,0x18,0x0c,0x18,0x18,0x30,0x00}, // 125 }
    {0x00,0x00,0x00,0x76,0xdc,0x00,0x00,0x00}, // 126 ~
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // 127
};

static void draw_char(Image &img, int x, int y, char c,
                       unsigned char r, unsigned char g, unsigned char b) {
    if (c < 32 || c > 127) c = 32;
    const unsigned char *glyph = font8x8[(unsigned char)c - 32];
    for (int row = 0; row < 8; ++row) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (0x80 >> col)) {
                img.set_pixel(x + col, y + row, r, g, b);
            }
        }
    }
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
        "  --vl 纬度        配合 --video 使用，指定观察纬度（默认0）\n  --purple         紫色辉光奇幻模式，大陆发出紫色辉光，海洋变为暗紫色\n",
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
    printf("纬度: %.1f\n", -lat0 * 180 / PI);
    printf("输出目录: %s\n\n", out_dir);
    if (purple_mode) printf("模式: 紫色辉光奇幻模式\n");

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

    // Count land/water from the original map (accurate, done once)
    double land_pct, water_pct;
    count_land_water_original(src, cx, cy, rx, ry, land_pct, water_pct);
    printf("陆地: %.1f%%, 海洋: %.1f%%\n", land_pct, water_pct);

    // Pre-allocate frame buffer (reuse for all frames)
    Image frame;
    frame.create(FW, FH, 0, 0, 0);

    // num_frames = 圈数 * 360, 所以总旋转角度 = 圈数 * 2*PI
    double total_rotation = 2.0 * PI * num_frames / 360.0;
    for (int f = 0; f < num_frames; ++f) {
        double lon0 = total_rotation * f / num_frames;

        // Clear frame to black (reuse buffer)
        memset(frame.data.data(), 0, frame.data.size());

        generate_stars(frame, FW, FH, 42 + f);
        render_frame(frame, ECX, ECY, ER, lon0, lat0,
                      ell, ecx, ecy, rx, ry);

        // Draw land/water percentage on frame (large text, 3x scaled)
        char info_text[64];
        snprintf(info_text, sizeof(info_text), "Land: %.1f%%  Water: %.1f%%", land_pct, water_pct);
        // Determine text color based on mode
        unsigned char tr = 255, tg = 255, tb = 255;
        if (purple_mode) { tr = 200; tg = 100; tb = 255; } // purple-ish white
        int text_x = 20;
        int text_y = FH - 40;
        for (int ci = 0; info_text[ci]; ++ci) {
            int cx = text_x + ci * 27;
            unsigned char ch = (unsigned char)info_text[ci];
            if (ch < 32) ch = 32;
            for (int row = 0; row < 8; ++row) {
                unsigned char bits = font8x8[ch - 32][row];
                for (int col = 0; col < 8; ++col) {
                    if (bits & (0x80 >> col)) {
                        for (int dy = 0; dy < 3; ++dy)
                            for (int dx = 0; dx < 3; ++dx)
                                frame.set_pixel(cx + col*3 + dx, text_y + row*3 + dy, tr, tg, tb);
                    }
                }
            }
        }

        char fn[256];
        snprintf(fn, sizeof(fn), "%s/frame_%04d.png", out_dir, f);
        frame.save_png(fn);

        // Progress indicator
        if ((f + 1) % 10 == 0 || f == 0 || f == num_frames - 1) {
            printf("  帧 %4d/%d (%.0f%%)\n", f + 1, num_frames, 100.0 * (f + 1) / num_frames);
        }
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
        if (ret == 0) {
            printf("视频已保存: %s\n", output_video);
            // 自动清理临时帧目录
            char rmcmd[512];
            snprintf(rmcmd, sizeof(rmcmd), "rm -rf %s", out_dir);
            system(rmcmd);
            printf("已清理临时帧目录: %s\n", out_dir);
        } else {
            fprintf(stderr, "视频合成失败 (ffmpeg返回 %d = %d>>8)\n", ret, ret>>8);
        }
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
    printf("经度: %.1f°, 纬度: %.1f°\n", lon0 * 180 / PI, -lat0 * 180 / PI);
    printf("输出目录: %s\n\n", out_dir);
    if (purple_mode) printf("模式: 紫色辉光奇幻模式\n");

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
    int lat_deg = (int)round(-lat0 * 180 / PI);
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
    printf("观察纬度: %.1f°\n", -lat0 * 180 / PI);
    if (purple_mode) printf("模式: 紫色辉光奇幻模式\n");
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

    // Stars background first, then render earth on top
    generate_stars(frame, FW, FH, 42);

    double land_pct, water_pct;

    // Count land/water from the original map (accurate)
    count_land_water_original(src, cx, cy, rx, ry, land_pct, water_pct);

    render_compare(frame, ECX, ECY, ER, lat0,
                    ell, ecx, ecy, rx, ry,
                    land_pct, water_pct);

    // Draw labels
    draw_text(frame, 10, 10, "0", 255, 255, 255);
    draw_text(frame, FW/2 + 10, 10, "180", 255, 255, 255);

    // Left side: green large digits for land percentage (2x size)
    char land_text[16];
    snprintf(land_text, sizeof(land_text), "%.1f%%", land_pct);
    int land_x = (FW/4 - (int)strlen(land_text) * 27) / 2;
    for (int ci = 0; land_text[ci]; ++ci) {
        int cx = land_x + ci * 27;
        unsigned char ch = (unsigned char)land_text[ci];
        if (ch < 32) ch = 32;
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = font8x8[ch - 32][row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    frame.set_pixel(cx + col*3, 50 + row*3, 0, 255, 0);
                    frame.set_pixel(cx + col*3 + 1, 50 + row*3, 0, 255, 0);
                    frame.set_pixel(cx + col*3 + 2, 50 + row*3, 0, 255, 0);
                    frame.set_pixel(cx + col*3, 50 + row*3 + 1, 0, 255, 0);
                    frame.set_pixel(cx + col*3 + 1, 50 + row*3 + 1, 0, 255, 0);
                    frame.set_pixel(cx + col*3 + 2, 50 + row*3 + 1, 0, 255, 0);
                    frame.set_pixel(cx + col*3, 50 + row*3 + 2, 0, 255, 0);
                    frame.set_pixel(cx + col*3 + 1, 50 + row*3 + 2, 0, 255, 0);
                    frame.set_pixel(cx + col*3 + 2, 50 + row*3 + 2, 0, 255, 0);
                }
            }
        }
    }

    // Right side: blue large digits for water percentage (2x size)
    char water_text[16];
    snprintf(water_text, sizeof(water_text), "%.1f%%", water_pct);
    int water_x = FW/2 + (FW/4 - (int)strlen(water_text) * 27) / 2;
    for (int ci = 0; water_text[ci]; ++ci) {
        int cx = water_x + ci * 27;
        unsigned char ch = (unsigned char)water_text[ci];
        if (ch < 32) ch = 32;
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = font8x8[ch - 32][row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    frame.set_pixel(cx + col*3, 50 + row*3, 0, 100, 255);
                    frame.set_pixel(cx + col*3 + 1, 50 + row*3, 0, 100, 255);
                    frame.set_pixel(cx + col*3 + 2, 50 + row*3, 0, 100, 255);
                    frame.set_pixel(cx + col*3, 50 + row*3 + 1, 0, 100, 255);
                    frame.set_pixel(cx + col*3 + 1, 50 + row*3 + 1, 0, 100, 255);
                    frame.set_pixel(cx + col*3 + 2, 50 + row*3 + 1, 0, 100, 255);
                    frame.set_pixel(cx + col*3, 50 + row*3 + 2, 0, 100, 255);
                    frame.set_pixel(cx + col*3 + 1, 50 + row*3 + 2, 0, 100, 255);
                    frame.set_pixel(cx + col*3 + 2, 50 + row*3 + 2, 0, 100, 255);
                }
            }
        }
    }

    char fn[256];
    int lat_deg = (int)round(-lat0 * 180 / PI);
    snprintf(fn, sizeof(fn), "%s/compare_%+03d.png", out_dir, lat_deg);
    frame.save_png(fn);
    printf("已保存: %s\n", fn);
    printf("陆地: %.1f%%, 海洋: %.1f%%\n", land_pct, water_pct);

    return 0;
}

// ---------------------------------------------------------------------------
// Mode 4: Process video — streaming mode (pipe-based)
// ffmpeg decodes → pipe raw RGB24 → C++ processes → pipe raw RGB24 → ffmpeg encodes
// No intermediate files, processes frame by frame.
// ---------------------------------------------------------------------------
static int mode_video(const char *input_video, const char *output_video,
                       double lat0, const char *out_dir) {
    printf("=== 处理视频 (流式) ===\n");
    printf("输入视频: %s\n", input_video);
    printf("输出视频: %s\n", output_video);
    printf("观察纬度: %.1f°\n", -lat0 * 180 / PI);
    if (purple_mode) printf("模式: 紫色辉光奇幻模式\n\n");

    // Step 1: Probe video to get frame count and dimensions
    printf("步骤1: 探测视频信息...\n");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,nb_frames,r_frame_rate "
        "-of default=noprint_wrappers=1 \"%s\" 2>/dev/null", input_video);
    FILE *fp = popen(cmd, "r");
    int vw = 0, vh = 0, num_frames = 0;
    double fps = 30.0;
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            char key[64], val[64];
            if (sscanf(line, "width=%63s", val) == 1) vw = atoi(val);
            if (sscanf(line, "height=%63s", val) == 1) vh = atoi(val);
            if (sscanf(line, "nb_frames=%63s", val) == 1) num_frames = atoi(val);
            if (sscanf(line, "r_frame_rate=%63s", val) == 1) {
                int n = 0, d = 1;
                if (sscanf(val, "%d/%d", &n, &d) == 2 && d > 0) fps = (double)n / d;
                else fps = atof(val);
                if (fps <= 0) fps = 30.0;
            }
        }
        pclose(fp);
    }
    if (vw <= 0 || vh <= 0) {
        fprintf(stderr, "无法获取视频信息\n");
        return 1;
    }
    if (num_frames <= 0) {
        // If nb_frames not available, estimate from duration
        snprintf(cmd, sizeof(cmd),
            "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1 \"%s\" 2>/dev/null",
            input_video);
        fp = popen(cmd, "r");
        double dur = 0;
        if (fp) {
            char buf[64];
            if (fgets(buf, sizeof(buf), fp)) dur = atof(buf);
            pclose(fp);
        }
        if (dur > 0) num_frames = (int)(dur * fps + 0.5);
        else num_frames = 0;
    }
    printf("  分辨率: %dx%d, 帧率: %.2f, 帧数: %d\n", vw, vh, fps, num_frames);
    if (num_frames <= 0) {
        fprintf(stderr, "无法确定帧数\n");
        return 1;
    }

    // Step 2: Detect ellipse on first frame
    printf("步骤2: 提取第一帧并检测椭圆...\n");
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -vframes 1 -f image2pipe -vcodec ppm - 2>/dev/null",
        input_video);
    fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "无法启动 ffmpeg 提取第一帧\n");
        return 1;
    }
    // Read PPM header
    char ppm_header[256];
    int ppm_w = 0, ppm_h = 0, ppm_max = 0;
    if (!fgets(ppm_header, sizeof(ppm_header), fp) ||
        !fgets(ppm_header, sizeof(ppm_header), fp) ||
        sscanf(ppm_header, "%d %d", &ppm_w, &ppm_h) != 2 ||
        !fgets(ppm_header, sizeof(ppm_header), fp) ||
        sscanf(ppm_header, "%d", &ppm_max) != 1) {
        pclose(fp);
        fprintf(stderr, "无法读取第一帧 PPM 数据\n");
        return 1;
    }
    Image first_frame;
    first_frame.create(ppm_w, ppm_h);
    size_t ppm_size = (size_t)ppm_w * ppm_h * 3;
    size_t read_bytes = fread(first_frame.data.data(), 1, ppm_size, fp);
    pclose(fp);
    if (read_bytes != ppm_size) {
        fprintf(stderr, "第一帧数据不完整 (%zu / %zu)\n", read_bytes, ppm_size);
        return 1;
    }
    printf("  第一帧: %dx%d\n", ppm_w, ppm_h);

    double cx, cy, rx, ry;
    if (!find_ellipse(first_frame, cx, cy, rx, ry)) {
        fprintf(stderr, "第一帧无法找到椭圆!\n");
        return 1;
    }
    Image ell;
    double ecx, ecy;
    extract_ellipse(first_frame, cx, cy, rx, ry, ell, ecx, ecy);

    const int FW = 1920, FH = 1080;
    const double ER = 420.0;
    const double ECX = FW / 4.0;
    const double ECY = FH / 2.0;

    // Step 3: Stream process — ffmpeg decode → pipe → process → pipe → ffmpeg encode
    printf("步骤3: 流式处理 %d 帧...\n", num_frames);

    // Open decoder: ffmpeg → raw RGB24 pipe
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -f rawvideo -pix_fmt rgb24 - 2>/dev/null",
        input_video);
    FILE *dec_pipe = popen(cmd, "r");
    if (!dec_pipe) {
        fprintf(stderr, "无法启动 ffmpeg 解码器\n");
        return 1;
    }

    // Open encoder: raw RGB24 pipe → ffmpeg → output video
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s %dx%d -framerate %.2f -i - "
        "-c:v libx264 -pix_fmt yuv420p -crf 23 -an "
        "\"%s\" 2>/dev/null",
        FW, FH, fps, output_video);
    FILE *enc_pipe = popen(cmd, "w");
    if (!enc_pipe) {
        pclose(dec_pipe);
        fprintf(stderr, "无法启动 ffmpeg 编码器\n");
        return 1;
    }

    // Allocate reusable frame buffers
    std::vector<unsigned char> raw_buf((size_t)vw * vh * 3);
    Image frame_src;
    frame_src.create(vw, vh);
    Image frame_ell;
    Image frame_ell_prev;  // previous frame's ellipse texture (for motion-compensated修复)
    bool has_prev_ell = false;
    Image frame_out;
    frame_out.create(FW, FH);
    Image frame_out_prev;  // previous frame's output (for image-space seam修复)
    frame_out_prev.create(FW, FH);
    bool has_prev_frame_out = false;

    size_t frame_size_src = (size_t)vw * vh * 3;
    size_t frame_size_out = (size_t)FW * FH * 3;

    int processed = 0;
    for (int f = 0; f < num_frames; ++f) {
        // Read raw frame from decoder pipe
        size_t nread = fread(raw_buf.data(), 1, frame_size_src, dec_pipe);
        if (nread != frame_size_src) {
            if (feof(dec_pipe)) break;
            fprintf(stderr, "  帧 %d: 读取不完整 (%zu / %zu)\n", f+1, nread, frame_size_src);
            continue;
        }

        // Copy raw data into frame_src
        memcpy(frame_src.data.data(), raw_buf.data(), frame_size_src);

        // Extract ellipse texture from this frame
        extract_ellipse(frame_src, cx, cy, rx, ry, frame_ell, ecx, ecy);

        // --- Motion-compensated修复: 利用前一帧的纹理修复当前帧的180°经线断层 ---
        // 原理: 对于当前帧纹理中mx接近±1(即180°经线附近)的像素,
        // 从前一帧纹理中采样同一经纬度的值,因为前一帧中该位置可能不在断层区域
        if (has_prev_ell) {
            int ew = frame_ell.w;
            int eh = frame_ell.h;
            for (int ey = 0; ey < eh; ++ey) {
                for (int ex = 0; ex < ew; ++ex) {
                    double nx = (ex - ecx) / rx;
                    double ny = (ey - ecy) / ry;
                    if (nx * nx + ny * ny > 1.0) continue;
                    
                    // Check if this pixel is near the ±180° meridian (mx near ±1)
                    // In the ellipse texture, the left/right edges correspond to mx=±1
                    // Pixels near the horizontal edges of the ellipse are prone to seams
                    double dist_to_edge = 1.0 - std::abs(nx);
                    if (dist_to_edge < 0.04) {  // Within 4% of the edge
                        auto dp = frame_ell.pix(ex, ey);
                        // If the pixel is suspiciously dark (possible seam artifact)
                        if (dp[0] < 10 && dp[1] < 10 && dp[2] < 10) {
                            // Sample from previous frame's texture at the same (nx, ny)
                            double prev_ex = ecx + nx * rx;
                            double prev_ey = ecy + ny * ry;
                            unsigned char pr = 0, pg = 0, pb = 0;
                            sample_bilinear(frame_ell_prev, prev_ex, prev_ey, pr, pg, pb);
                            if (pr > 10 || pg > 10 || pb > 10) {
                                dp[0] = pr; dp[1] = pg; dp[2] = pb;
                            }
                        }
                    }
                }
            }
        }
        // Save current ellipse as previous for next frame
        frame_ell_prev = frame_ell;
        has_prev_ell = true;

        // Clear output frame
        memset(frame_out.data.data(), 0, frame_size_out);

        // Stars background
        generate_stars(frame_out, FW, FH, 42 + f);

        // Render compare view
        double land_pct, water_pct;

        // Count land/water from the original first frame (accurate)
        // Use the ellipse parameters detected from the first frame
        count_land_water_original(first_frame, cx, cy, rx, ry, land_pct, water_pct);

        render_compare(frame_out, ECX, ECY, ER, lat0,
                        frame_ell, ecx, ecy, rx, ry,
                        land_pct, water_pct);

        // --- 图像空间后处理: 修复180°视角中的经线断层 ---
        // 在右半部分(180°视角)中,球体中央竖线附近(180°经线)可能出现接缝
        // 利用前一帧的渲染结果来修复: 对于同一地理位置,前一帧的像素可能不在断层区域
        if (has_prev_frame_out) {
            int seam_x_start = FW/2 + (int)(ECX + FW/4 - ER * 0.08);  // 180°经线左侧
            int seam_x_end   = FW/2 + (int)(ECX + FW/4 + ER * 0.08);  // 180°经线右侧
            for (int py = 0; py < FH; ++py) {
                for (int px = seam_x_start; px <= seam_x_end; ++px) {
                    auto dp = frame_out.pix(px, py);
                    // Detect seam: unusually dark vertical line in the middle of the earth
                    if (dp[0] < 15 && dp[1] < 15 && dp[2] < 15) {
                        // Check neighbors to confirm this is a seam (not space background)
                        int neighbor_count = 0;
                        if (px > 0) {
                            auto np = frame_out.pix(px-1, py);
                            if (np[0] > 15 || np[1] > 15 || np[2] > 15) neighbor_count++;
                        }
                        if (px < FW-1) {
                            auto np = frame_out.pix(px+1, py);
                            if (np[0] > 15 || np[1] > 15 || np[2] > 15) neighbor_count++;
                        }
                        if (neighbor_count >= 1) {
                            // This is a seam pixel, repair from previous frame
                            auto prev_p = frame_out_prev.pix(px, py);
                            if (prev_p[0] > 15 || prev_p[1] > 15 || prev_p[2] > 15) {
                                dp[0] = prev_p[0]; dp[1] = prev_p[1]; dp[2] = prev_p[2];
                            }
                        }
                    }
                }
            }
        }
        // Save current frame output as previous for next frame
        memcpy(frame_out_prev.data.data(), frame_out.data.data(), frame_size_out);
        has_prev_frame_out = true;

        // Draw labels
        draw_text(frame_out, 10, 10, "0", 255, 255, 255);
        draw_text(frame_out, FW/2 + 10, 10, "180", 255, 255, 255);

        // Left side: green land percentage
        char land_text[16];
        snprintf(land_text, sizeof(land_text), "%.1f%%", land_pct);
        int land_x = (FW/4 - (int)strlen(land_text) * 27) / 2;
        for (int ci = 0; land_text[ci]; ++ci) {
            int cx2 = land_x + ci * 27;
            unsigned char ch = (unsigned char)land_text[ci];
            if (ch < 32) ch = 32;
            for (int row = 0; row < 8; ++row) {
                unsigned char bits = font8x8[ch - 32][row];
                for (int col = 0; col < 8; ++col) {
                    if (bits & (0x80 >> col)) {
                        for (int dy = 0; dy < 3; ++dy)
                            for (int dx = 0; dx < 3; ++dx)
                                frame_out.set_pixel(cx2 + col*3 + dx, 50 + row*3 + dy, 0, 255, 0);
                    }
                }
            }
        }

        // Right side: blue water percentage
        char water_text[16];
        snprintf(water_text, sizeof(water_text), "%.1f%%", water_pct);
        int water_x = FW/2 + (FW/4 - (int)strlen(water_text) * 27) / 2;
        for (int ci = 0; water_text[ci]; ++ci) {
            int cx2 = water_x + ci * 27;
            unsigned char ch = (unsigned char)water_text[ci];
            if (ch < 32) ch = 32;
            for (int row = 0; row < 8; ++row) {
                unsigned char bits = font8x8[ch - 32][row];
                for (int col = 0; col < 8; ++col) {
                    if (bits & (0x80 >> col)) {
                        for (int dy = 0; dy < 3; ++dy)
                            for (int dx = 0; dx < 3; ++dx)
                                frame_out.set_pixel(cx2 + col*3 + dx, 50 + row*3 + dy, 0, 100, 255);
                    }
                }
            }
        }

        // Write raw RGB24 to encoder pipe
        fwrite(frame_out.data.data(), 1, frame_size_out, enc_pipe);

        processed++;
        if (processed % 30 == 0 || processed == num_frames)
            printf("  帧 %4d/%d\n", processed, num_frames);
    }

    // Close pipes
    pclose(dec_pipe);
    int enc_ret = pclose(enc_pipe);

    printf("\n完成! 处理 %d 帧\n", processed);
    if (enc_ret == 0) {
        printf("视频已保存: %s\n", output_video);
    } else {
        fprintf(stderr, "视频合成可能有问题 (ffmpeg返回 %d = %d>>8)\n", enc_ret, enc_ret>>8);
    }

    return enc_ret == 0 ? 0 : 1;
}

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
            else if (strcmp(argv[i], "--purple") == 0)
                purple_mode = true;

            else if (strcmp(argv[i], "--vl") == 0 && i + 1 < argc)
                lat0 = -atof(argv[++i]) * PI / 180.0;
            else if (i == argc - 1 && argv[i][0] != '-')
                out_dir = argv[i];
        }

        return mode_video(input_video, output_video, lat0, out_dir);
    }

    // Normal mode
    setvbuf(stdout, NULL, _IONBF, 0);
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
    if (pos < argc && (argv[pos][0] != '-' || (argv[pos][0] == '-' && argv[pos][1] >= '0' && argv[pos][1] <= '9'))) {
        lat0 = -atof(argv[pos]) * PI / 180.0;
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
            single_lat = -atof(argv[++i]) * PI / 180.0;
        else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            compare_mode = true;
            compare_lat = -atof(argv[++i]) * PI / 180.0;
        } else if (strcmp(argv[i], "--purple") == 0)
            purple_mode = true;
    }

    if (compare_mode)
        return mode_compare(input_file, compare_lat, out_dir);
    else if (single_mode)
        return mode_single(input_file, single_lon, single_lat, out_dir);
    else
        return mode_rotate(input_file, num_frames, lat0, out_dir,
                           frames_only ? nullptr : output_video, frames_only);
}