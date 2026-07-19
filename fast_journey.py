#!/usr/bin/env python3
"""
快速旅程视频生成器 v3
ffmpeg解码 → C++渲染（带海陆比例+正确纬度） → ffmpeg编码

用法:
  python3 fast_journey.py input.mp4 output.mp4 [--split 86] [--purple]
"""

import subprocess
import os
import sys
import signal
import math
import re
from PIL import Image

PI = 3.14159265358979323846
FW, FH = 1920, 1080


def count_land_water(img, cx, cy, rx, ry):
    pixels = list(img.getdata())
    w, h = img.size
    total = 0
    land = 0
    for y in range(h):
        for x in range(w):
            nx = (x - cx) / rx
            ny = (y - cy) / ry
            if nx * nx + ny * ny > 1.0:
                continue
            p = pixels[y * w + x]
            if max(p) < 3:
                continue
            total += 1
            r, g, b = p
            is_water = (b > r and b > g and (b - r) > 10)
            if not is_water:
                land += 1
    return 100.0 * land / total if total > 0 else 0.0


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


def extract_first_frame_ppm(input_video):
    proc = subprocess.Popen(
        f'ffmpeg -c:v h264 -y -i "{input_video}" -vframes 1 -f image2pipe -vcodec ppm - 2>/dev/null',
        shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    stdout, _ = proc.communicate(timeout=30)
    return stdout


def parse_ppm(ppm_data):
    idx = ppm_data.find(b'\n')
    rest = ppm_data[idx+1:]
    idx = rest.find(b'\n')
    w, h = map(int, rest[:idx].split())
    rest = rest[idx+1:]
    idx = rest.find(b'\n')
    pixel_data = rest[idx+1:]
    return w, h, pixel_data


def main():
    if len(sys.argv) < 3:
        print("用法: python3 fast_journey.py input.mp4 output.mp4 [--split 86] [--purple]")
        sys.exit(1)
    
    input_video = sys.argv[1]
    output_video = sys.argv[2]
    split_sec = 86
    purple = False
    for i in range(3, len(sys.argv)):
        if sys.argv[i] == '--split' and i + 1 < len(sys.argv):
            split_sec = int(sys.argv[i + 1])
        elif sys.argv[i] == '--purple':
            purple = True
    
    print(f"输入: {input_video}")
    print(f"输出: {output_video}")
    if purple: print("模式: 紫色辉光")
    
    # 获取视频信息
    info = get_video_info(input_video)
    vw, vh = info['width'], info['height']
    num_frames = info['num_frames']
    fps = info['fps']
    print(f"分辨率: {vw}x{vh}, 帧率: {fps:.2f}, 总帧数: {num_frames}")
    
    split_frame = int(split_sec * fps + 0.5)
    if split_frame >= num_frames: split_frame = num_frames // 2
    print(f"分界点: 第 {split_frame} 帧 ({split_frame/fps:.1f}s)")
    print(f"前半段: 南回归线(-23.5°)→赤道(0°), 180°→0°")
    print(f"后半段: 赤道(0°)→北回归线(+23.5°), 0°→180°")
    
    # 提取第一帧
    print("\n检测椭圆...")
    ppm_data = extract_first_frame_ppm(input_video)
    w, h, pixel_data = parse_ppm(ppm_data)
    img = Image.frombytes('RGB', (w, h), pixel_data)
    first_frame_path = 'video/first_frame.png'
    img.save(first_frame_path)
    
    # 用C++检测椭圆
    cwd = os.path.dirname(os.path.abspath(__file__))
    globe_exe = os.path.join(cwd, 'build/globe')
    
    detect_proc = subprocess.Popen(
        [globe_exe, first_frame_path, '-f', '--lon', '0', '--lat', '0'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    stdout, _ = detect_proc.communicate(timeout=30)
    result_text = stdout.decode()
    
    # 解析椭圆参数
    ellipse_info = {}
    for line in result_text.split('\n'):
        m = re.search(r'center=\(([\d.]+),([\d.]+)\)', line)
        if m:
            ellipse_info['cx'] = float(m.group(1))
            ellipse_info['cy'] = float(m.group(2))
        m = re.search(r'rx=([\d.]+)', line)
        if m: ellipse_info['rx'] = float(m.group(1))
        m = re.search(r'ry=([\d.]+)', line)
        if m: ellipse_info['ry'] = float(m.group(1))
    
    if not ellipse_info:
        print("无法解析椭圆参数!")
        sys.exit(1)
    
    cx, cy, rx, ry = ellipse_info['cx'], ellipse_info['cy'], ellipse_info['rx'], ellipse_info['ry']
    print(f"椭圆: center=({cx:.1f},{cy:.1f}), rx={rx:.1f}, ry={ry:.1f}")
    
    # 统计海陆比例
    print("统计海陆比例...")
    land_pct = count_land_water(img, cx, cy, rx, ry)
    print(f"海陆比例: Land={land_pct:.1f}%")
    
    # 启动解码器
    print("\n启动管道...")
    dec_proc = subprocess.Popen(
        f'ffmpeg -c:v h264 -y -i "{input_video}" -f rawvideo -pix_fmt rgb24 - 2>/dev/null',
        shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    
    # 启动编码器
    enc_proc = subprocess.Popen(
        f'ffmpeg -y -f rawvideo -pix_fmt rgb24 -s {FW}x{FH} -framerate {fps:.2f} -i - '
        f'-c:v libx264 -pix_fmt yuv420p -crf 23 -an "{output_video}" 2>/dev/null',
        shell=True, stdin=subprocess.PIPE,
        preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
    )
    
    frame_size_src = vw * vh * 3
    processed = 0
    purple_flag = '1' if purple else '0'
    
    print(f"\n逐帧处理 {num_frames} 帧...")
    
    try:
        for f in range(num_frames):
            raw_data = dec_proc.stdout.read(frame_size_src)
            if len(raw_data) != frame_size_src:
                print(f"读到 {len(raw_data)} 字节，提前结束")
                break
            
            # 计算视角
            # 前半段: 南回归线(-23.5°)→赤道(0°), 经度180°→0°
            # 后半段: 赤道(0°)→北回归线(+23.5°), 经度0°→180°
            if f < split_frame:
                t = f / split_frame
                lat0 = -23.5 + t * 23.5
                lon0 = 180.0 - t * 180.0
                phase = "Capricorn->Equator"
            else:
                t = (f - split_frame) / (num_frames - split_frame)
                lat0 = t * 23.5
                lon0 = t * 180.0
                phase = "Equator->Cancer"
            
            # 调用C++渲染程序
            render_proc = subprocess.Popen(
                f'{globe_exe} --render-frame {vw} {vh} {cx} {cy} {rx} {ry} {lon0} {lat0} {purple_flag} {land_pct}',
                shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                preexec_fn=lambda: os.setsid() if hasattr(os, 'setsid') else None
            )
            rendered_data, render_err = render_proc.communicate(input=raw_data, timeout=30)
            
            if len(rendered_data) == FW * FH * 3:
                enc_proc.stdin.write(rendered_data)
            else:
                enc_proc.stdin.write(b'\x00' * FW * FH * 3)
                if render_err:
                    print(f"  渲染错误: {render_err[:100]}")
            
            processed += 1
            if processed % 30 == 0 or processed == 1 or processed == num_frames:
                print(f"  {processed:5d}/{num_frames} ({100*processed/num_frames:.0f}%) [{phase}] Lat={lat0:+.1f} Lon={lon0:.0f}")
    
    except BrokenPipeError:
        print(f"管道断开 (已处理 {processed} 帧)")
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
