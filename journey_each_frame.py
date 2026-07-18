#!/usr/bin/env python3
"""
逐帧旅程视频生成器
对输入视频的每一帧提取纹理，用不同视角渲染输出

用法:
  python3 journey_each_frame.py input.mp4 output.mp4 [--split 86] [--purple]
"""

import subprocess
import os
import sys
import signal
import math
from PIL import Image, ImageDraw, ImageFont

PI = 3.14159265358979323846
SHRINK = 0.966
FW, FH = 1920, 1080
ER = 420.0
ECX, ECY = FW / 2.0, FH / 2.0
purple_mode = False


def run_ffmpeg(cmd, input_data=None, timeout=600):
    proc = subprocess.Popen(
        cmd, shell=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        stdin=subprocess.PIPE if input_data is not None else None,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    try:
        stdout, stderr = proc.communicate(input=input_data, timeout=timeout)
        return stdout, stderr, proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        raise


def get_video_info(input_video):
    r = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
         '-show_entries', 'stream=width,height,nb_frames,r_frame_rate',
         '-of', 'default=noprint_wrappers=1', input_video],
        capture_output=True, text=True, timeout=30)
    info = {'width': 0, 'height': 0, 'num_frames': 0, 'fps': 30.0}
    for line in r.stdout.split('\n'):
        if '=' in line:
            k, v = line.split('=', 1)
            if k == 'width': info['width'] = int(v)
            elif k == 'height': info['height'] = int(v)
            elif k == 'nb_frames': info['num_frames'] = int(v)
            elif k == 'r_frame_rate':
                if '/' in v:
                    n, d = v.split('/')
                    info['fps'] = float(n) / float(d)
                else: info['fps'] = float(v)
    if info['num_frames'] <= 0:
        r = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
             '-of', 'default=noprint_wrappers=1', input_video],
            capture_output=True, text=True, timeout=30)
        dur = float(r.stdout.strip())
        if dur > 0: info['num_frames'] = int(dur * info['fps'] + 0.5)
    return info


def find_ellipse_from_ppm(ppm_data):
    """直接从PPM数据检测椭圆，不创建PIL图像"""
    idx = ppm_data.find(b'\n')
    rest = ppm_data[idx+1:]
    idx = rest.find(b'\n')
    w, h = map(int, rest[:idx].split())
    rest = rest[idx+1:]
    idx = rest.find(b'\n')
    pixel_data = rest[idx+1:]
    
    # 转为像素列表
    pixels = []
    for i in range(0, len(pixel_data), 3):
        pixels.append((pixel_data[i], pixel_data[i+1], pixel_data[i+2]))
    
    # 质心
    sx = sy = cnt = 0
    for y in range(h):
        for x in range(w):
            p = pixels[y * w + x]
            if max(p) > 8:
                sx += x; sy += y; cnt += 1
    if cnt == 0: return None, None, None, None, None
    cx, cy = sx / cnt, sy / cnt
    
    # 径向扫描
    N = 720
    maxd = max(w, h)
    radii = []
    for a in range(N):
        ang = a * 2 * PI / N
        dx, dy = math.cos(ang), math.sin(ang)
        in_map = False
        found = False
        for r in range(1, maxd):
            x = int(round(cx + r * dx))
            y = int(round(cy + r * dy))
            if x < 0 or x >= w or y < 0 or y >= h:
                radii.append(r); found = True; break
            p = pixels[y * w + x]
            maxc = max(p)
            if maxc > 10: in_map = True
            elif in_map and maxc < 6:
                radii.append(r); found = True; break
        if not found: radii.append(maxd)
    
    mnx, mxx = w, 0
    mny, mxy = h, 0
    for a in range(N):
        ang = a * 2 * PI / N
        x = cx + radii[a] * math.cos(ang)
        y = cy + radii[a] * math.sin(ang)
        mnx = min(mnx, x); mxx = max(mxx, x)
        mny = min(mny, y); mxy = max(mxy, y)
    
    cx = (mnx + mxx) / 2
    cy = (mny + mxy) / 2
    rx = (mxx - mnx) / 2 * SHRINK
    ry = (mxy - mny) / 2 * SHRINK
    return cx, cy, rx, ry, pixels, w, h


def extract_texture_from_pixels(pixels, w, h, cx, cy, rx, ry):
    ew = int(math.ceil(4 * rx)) + 3
    eh = int(math.ceil(2 * ry)) + 3
    if ew % 2 == 0: ew += 1
    if eh % 2 == 0: eh += 1
    ecx, ecy = ew / 2.0, eh / 2.0
    tex = [(0, 0, 0)] * (ew * eh)
    for ey in range(eh):
        for ex in range(ew):
            nx = (ex - ecx) / rx
            ny = (ey - ecy) / ry
            if nx * nx + ny * ny <= 1.0:
                sx = cx + nx * rx
                sy = cy + ny * ry
                x0, y0 = int(math.floor(sx)), int(math.floor(sy))
                x1, y1 = min(x0+1, w-1), min(y0+1, h-1)
                x0, y0 = max(0, x0), max(0, y0)
                fx, fy = sx - x0, sy - y0
                p00 = pixels[y0 * w + x0]
                p10 = pixels[y0 * w + x1]
                p01 = pixels[y1 * w + x0]
                p11 = pixels[y1 * w + x1]
                w00 = (1-fx)*(1-fy); w10 = fx*(1-fy); w01 = (1-fx)*fy; w11 = fx*fy
                r = int(max(0, min(255, p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11)))
                g = int(max(0, min(255, p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11)))
                b = int(max(0, min(255, p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11)))
                tex[ey * ew + ex] = (r, g, b)
    return tex, ecx, ecy, ew, eh


def mollweide_forward(lat, lon):
    theta = lat
    for _ in range(4):
        s2t = math.sin(2.0 * theta)
        f = 2.0 * theta + s2t - PI * math.sin(lat)
        df = 2.0 + 2.0 * math.cos(2.0 * theta)
        if abs(df) < 1e-12: break
        theta -= f / df
    theta = max(-PI/2, min(PI/2, theta))
    return lon / PI * math.cos(theta), math.sin(theta)


def render_frame(tex, ew, eh, ecx, ecy, rx, ry, lon0, lat0):
    frame = [(0, 0, 0)] * (FW * FH)
    Xx, Xy, Xz = -math.sin(lon0), 0, math.cos(lon0)
    Yx = -math.sin(lat0) * math.cos(lon0)
    Yy = math.cos(lat0)
    Yz = -math.sin(lat0) * math.sin(lon0)
    Zx = math.cos(lat0) * math.cos(lon0)
    Zy = math.sin(lat0)
    Zz = math.cos(lat0) * math.sin(lon0)
    
    for py in range(FH):
        for px in range(FW):
            nx = (px - ECX) / ER
            ny = (py - ECY) / ER
            nz2 = 1.0 - nx*nx - ny*ny
            if nz2 < 0: continue
            nz = math.sqrt(nz2)
            px3 = nx * Xx + ny * Yx + nz * Zx
            py3 = nx * Xy + ny * Yy + nz * Zy
            pz3 = nx * Xz + ny * Yz + nz * Zz
            lat = math.asin(max(-1.0, min(1.0, py3)))
            lon = math.atan2(pz3, px3)
            mx, my = mollweide_forward(lat, lon)
            if mx*mx + my*my > 1.0: continue
            wmx = mx
            while wmx > 1.0: wmx -= 2.0
            while wmx < -1.0: wmx += 2.0
            ex = ecx + wmx * rx
            ey = ecy + my * ry
            if 0 <= ex < ew-1 and 0 <= ey < eh-1:
                x0, y0 = int(ex), int(ey)
                fx, fy = ex - x0, ey - y0
                p00 = tex[y0 * ew + x0]
                p10 = tex[y0 * ew + min(x0+1, ew-1)]
                p01 = tex[min(y0+1, eh-1) * ew + x0]
                p11 = tex[min(y0+1, eh-1) * ew + min(x0+1, ew-1)]
                w00 = (1-fx)*(1-fy); w10 = fx*(1-fy); w01 = (1-fx)*fy; w11 = fx*fy
                r = int(max(0, min(255, p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11)))
                g = int(max(0, min(255, p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11)))
                b = int(max(0, min(255, p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11)))
                rim = 1.0 - nz
                glow = 1.0 + 0.2 * rim * rim
                r = min(255, int(r * glow))
                g = min(255, int(g * glow))
                b = min(255, int(b * glow))
                if purple_mode:
                    maxc = max(r, g, b)
                    if maxc > 2:
                        bright = maxc / 255.0
                        is_water = (b > r and b > g and (b - r) > 10)
                        if is_water:
                            r = min(255, int(25 + bright * 55))
                            g = min(255, int(5 + bright * 20))
                            b = min(255, int(45 + bright * 75))
                        else:
                            r = min(255, int(120 + bright * 110))
                            g = min(255, int(15 + bright * 30))
                            b = min(255, int(160 + bright * 95))
                            if bright > 0.35:
                                extra = int((bright - 0.35) * 1.5 * 80)
                                r = min(255, r + extra)
                                b = min(255, b + extra)
                frame[py * FW + px] = (r, g, b)
    return frame


def add_stars(frame, seed):
    import random
    random.seed(seed)
    for _ in range(500):
        x = random.randint(0, FW-1)
        y = random.randint(0, FH-1)
        bright = 40 + random.randint(0, 179)
        size = 1 + random.randint(0, 1)
        for dy in range(-size, size+1):
            for dx in range(-size, size+1):
                px, py = x + dx, y + dy
                if 0 <= px < FW and 0 <= py < FH:
                    dist = math.sqrt(dx*dx + dy*dy)
                    b = int(bright * max(0, 1.0 - dist / (size + 1)))
                    idx = py * FW + px
                    frame[idx] = (min(255, frame[idx][0] + b),
                                  min(255, frame[idx][1] + b),
                                  min(255, frame[idx][2] + b))


def main():
    global purple_mode
    if len(sys.argv) < 3:
        print("用法: python3 journey_each_frame.py input.mp4 output.mp4 [--split 86] [--purple]")
        sys.exit(1)
    
    input_video = sys.argv[1]
    output_video = sys.argv[2]
    split_sec = 86
    for i in range(3, len(sys.argv)):
        if sys.argv[i] == '--split' and i + 1 < len(sys.argv):
            split_sec = int(sys.argv[i + 1])
        elif sys.argv[i] == '--purple':
            purple_mode = True
    
    print(f"输入: {input_video}")
    print(f"输出: {output_video}")
    if purple_mode: print("模式: 紫色辉光")
    
    # 获取视频信息
    info = get_video_info(input_video)
    vw, vh = info['width'], info['height']
    num_frames = info['num_frames']
    fps = info['fps']
    print(f"分辨率: {vw}x{vh}, 帧率: {fps:.2f}, 总帧数: {num_frames}")
    
    split_frame = int(split_sec * fps + 0.5)
    if split_frame >= num_frames: split_frame = num_frames // 2
    print(f"分界点: 第 {split_frame} 帧 ({split_frame/fps:.1f}s)")
    print(f"前半段: 0~{split_frame-1}  后半段: {split_frame}~{num_frames-1}")
    
    # 提取第一帧检测椭圆参数（所有帧共用）
    print("\n检测椭圆...")
    stdout, _, _ = run_ffmpeg(
        f'ffmpeg -c:v h264 -y -i "{input_video}" -vframes 1 -f image2pipe -vcodec ppm - 2>/dev/null',
        timeout=30)
    
    result = find_ellipse_from_ppm(stdout)
    if result[0] is None:
        print("无法找到椭圆!"); sys.exit(1)
    cx, cy, rx, ry, first_pixels, w, h = result
    print(f"椭圆: center=({cx:.1f},{cy:.1f}), rx={rx:.1f}, ry={ry:.1f}")
    
    # 启动编码器
    print("\n启动编码器...")
    enc_proc = subprocess.Popen(
        f'ffmpeg -y -f rawvideo -pix_fmt rgb24 -s {FW}x{FH} -framerate {fps:.2f} -i - '
        f'-c:v libx264 -pix_fmt yuv420p -crf 23 -an "{output_video}" 2>/dev/null',
        shell=True, stdin=subprocess.PIPE,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    
    # 启动解码器
    print("启动解码器...")
    dec_proc = subprocess.Popen(
        f'ffmpeg -c:v h264 -y -i "{input_video}" -f rawvideo -pix_fmt rgb24 - 2>/dev/null',
        shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    
    frame_size_src = vw * vh * 3
    processed = 0
    
    print(f"\n逐帧处理 {num_frames} 帧...")
    try:
        for f in range(num_frames):
            # 读取原始帧
            raw_data = dec_proc.stdout.read(frame_size_src)
            if len(raw_data) != frame_size_src:
                print(f"  读到 {len(raw_data)} 字节，预期 {frame_size_src}，提前结束")
                break
            
            # 从原始帧提取纹理
            pixels = []
            for i in range(0, len(raw_data), 3):
                pixels.append((raw_data[i], raw_data[i+1], raw_data[i+2]))
            tex, ecx, ecy, ew, eh = extract_texture_from_pixels(pixels, vw, vh, cx, cy, rx, ry)
            
            # 计算视角
            if f < split_frame:
                t = f / split_frame
                lat0 = (-23.5 + t * 23.5) * PI / 180.0
                lon0 = (180.0 - t * 180.0) * PI / 180.0
                phase = "Capricorn->Equator"
            else:
                t = (f - split_frame) / (num_frames - split_frame)
                lat0 = (t * 23.5) * PI / 180.0
                lon0 = (t * 180.0) * PI / 180.0
                phase = "Equator->Cancer"
            
            # 渲染
            frame_pixels = render_frame(tex, ew, eh, ecx, ecy, rx, ry, lon0, lat0)
            add_stars(frame_pixels, 42 + f)
            
            # 加文字
            img = Image.new('RGB', (FW, FH))
            img.putdata(frame_pixels)
            draw = ImageDraw.Draw(img)
            lat_deg = int(round(lat0 * 180 / PI))
            lon_deg = int(round(lon0 * 180 / PI))
            try:
                font = ImageFont.truetype("/system/fonts/DroidSansMono.ttf", 24)
            except:
                font = ImageFont.load_default()
            draw.text((20, 20), phase, fill=(200, 200, 255), font=font)
            draw.text((FW - 280, 20), f"Lat: {lat_deg:+d}  Lon: {lon_deg}", fill=(255, 255, 100), font=font)
            
            # 写入编码器
            enc_proc.stdin.write(img.tobytes())
            
            processed += 1
            if processed % 30 == 0 or processed == 1 or processed == num_frames:
                print(f"  {processed:5d}/{num_frames} ({100*processed/num_frames:.0f}%) [{phase}] Lat={lat_deg:+d} Lon={lon_deg}")
    
    except BrokenPipeError:
        print(f"编码器断开 (已处理 {processed} 帧)")
    except Exception as e:
        print(f"错误: {e}")
    finally:
        dec_proc.stdout.close()
        dec_proc.wait()
        enc_proc.stdin.close()
        enc_proc.wait()
    
    print(f"\n完成! 处理 {processed} 帧")
    print(f"视频: {output_video}")


if __name__ == '__main__':
    main()
