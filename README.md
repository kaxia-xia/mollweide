# MapBeauty — 地球旋转视频帧生成器

将摩尔维特（Mollweide）投影的世界地图转换为旋转地球3D动画，支持卫星云图风格渲染、0°/180° 双视角对比图、视频逐帧处理、旅程模式（纬度/经度渐变）等功能。

## 效果预览

- **旋转地球视频**：输入一张摩尔维特投影地图，生成地球自转动画（1920×1080, 30fps）
- **双视角对比图**：左右并排显示 0° 和 180° 经线视角，顶部标注陆地/海洋面积百分比
- **视频逐帧处理**：输入一个视频，对每一帧生成双视角对比图，再合成新视频
- **旅程模式**：从南回归线到北回归线的纬度/经度渐变动画，支持逐帧纹理提取

## 依赖

### 编译依赖
- C++20 编译器（g++ / clang++）
- CMake ≥ 3.5
- stb_image.h / stb_image_write.h（单头文件库，已包含在代码中）
- libpng
- libjpeg

### 运行依赖
- ffmpeg（用于视频帧提取和合成）
- Python 3 + Pillow（用于旅程视频生成）

### 在 Termux 中安装依赖

```bash
pkg update
pkg install cmake gcc libpng libjpeg-turbo ffmpeg python python-pillow
```

### 在 Linux 中安装依赖

```bash
# Debian/Ubuntu
sudo apt install cmake g++ libpng-dev libjpeg-dev ffmpeg python3 python3-pip
pip3 install Pillow

# 安装 stb 头文件
sudo apt install libstb-dev
# 或者手动下载 stb_image.h 和 stb_image_write.h 到 /usr/local/include/stb/
```

## 编译

```bash
mkdir -p build && cd build
cmake ..
make -j4
```

编译成功后会在 `build/` 目录下生成 `globe` 可执行文件。

## 使用说明

> 以下命令均在项目根目录下执行，`build/globe` 为可执行文件路径。

### 1. 生成旋转地球视频

```bash
build/globe test/test5.jpg
```

默认参数：1圈（360帧，12秒@30fps），赤道视角，输出 `output.mp4`。

旋转视频的每一帧右下角会显示陆地/海洋面积占比（基于原始地图统计）。

### 2. 指定圈数

```bash
build/globe test/test5.jpg 2        # 2圈（720帧，24秒）
build/globe test/test5.jpg 3        # 3圈（1080帧，36秒）
```

### 3. 指定视角纬度

```bash
build/globe test/test5.jpg 1 27         # 北纬27°
build/globe test/test5.jpg 1 -30        # 南纬30°
build/globe test/test5.jpg 1 90         # 北极俯视
```

### 4. 指定输出视频文件

```bash
build/globe test/test5.jpg -o myvideo.mp4
```

### 5. 只生成帧图片，不合成视频

```bash
build/globe test/test5.jpg -f
```

### 6. 生成指定经度的单张图片

```bash
build/globe test/test5.jpg -f --lon 180          # 180°经线
build/globe test/test5.jpg -f --lon 90 --lat 27  # 北纬27°, 90°经线
```

### 7. 生成 0°/180° 双视角对比图（带陆地/海洋比例）

```bash
build/globe test/test5.jpg --compare 27    # 北纬27°观察
build/globe test/test5.jpg --compare 0     # 赤道
build/globe test/test5.jpg --compare -30   # 南纬30°
```

生成 `frames/compare_+27.png`，左半为0°经线视角，右半为180°经线视角，顶部显示陆地/海洋百分比。

### 8. 处理视频（逐帧生成对比图再合成新视频）

```bash
build/globe --video input.mp4 -o output.mp4 --vl 27
```

参数说明：
- `--video`：输入视频文件路径
- `--vl`：观察纬度（默认0）
- `-o`：输出视频文件路径
- 可选：在最后加输出目录名（默认 `frames`）

### 9. 紫色辉光奇幻模式

```bash
build/globe test/test5.jpg --purple
build/globe test/test5.jpg --compare 27 --purple
build/globe test/test5.jpg -f --lon 180 --purple
build/globe --video input.mp4 -o output.mp4 --vl 27 --purple
```

在任意模式后加上 `--purple` 参数，即可启用紫色辉光奇幻效果：
- **大陆**：发出明亮的紫色/品红色辉光
- **海洋**：变为较暗的紫色/靛蓝色
- **大气辉光**：变为紫色调

### 10. 旅程模式（纬度/经度渐变视频）

从摩尔维特投影视频生成旅程动画：
- **前半段**：视角从南回归线(-23.5°)渐变到赤道(0°)，经度从180°向西到0°
- **后半段**：视角从赤道(0°)渐变到北回归线(+23.5°)，经度从0°向东到180°
- 每帧独立统计并显示海陆面积占比
- 支持紫色辉光模式

```bash
python3 fast_journey.py input.mp4 output.mp4 [--split 86] [--purple]
示例：
```bash
# 完整旅程，86秒分界，紫色辉光
python3 fast_journey.py a.mp4 journey_output.mp4 --split 86 --purple
```

参数说明：
- `input.mp4`：输入视频（摩尔维特投影）
- `output.mp4`：输出视频
- `--split 86`：前后段分界点（秒，默认86）
- `--purple`：紫色辉光模式

示例：
```bash
# 完整旅程，86秒分界，紫色辉光
python3 fast_journey.py video/a.mp4 journey_output.mp4 --split 86 --purple
```

### 完整参数列表

```
用法: build/globe <输入图片> [圈数] [纬度] [输出目录] [-o 视频文件] [-f] [--lon 经度] [--compare 纬度] [--video 视频文件] [--vl 纬度] [--purple]

位置参数:
  输入图片         摩尔维特投影的世界地图图片
  圈数             默认1圈（360帧，12秒@30fps）
  纬度             视角纬度，默认0（赤道），正数=北纬，负数=南纬
  输出目录         帧图片输出目录（默认 frames）

选项:
  -o 视频文件      指定输出视频文件路径（默认 output.mp4）
  -f               只生成帧图片，不合成视频
  --lon 经度       指定中心经度，与 -f 配合只生成一张该经度的图片
  --lat 纬度       指定视角纬度，与 --lon 配合使用
  --compare 纬度   生成0°和180°并排对比图，参数为观察纬度
  --video 视频文件 输入视频文件，逐帧处理为对比图再合成新视频
  --vl 纬度        配合 --video 使用，指定观察纬度（默认0）
  --purple         紫色辉光奇幻模式，大陆发出紫色辉光，海洋变为暗紫色
```

## 输出规格

- **帧图片**：1920×1080 PNG，底部显示陆地/海洋面积百分比
- **视频**：H.264 (libx264), yuv420p, CRF 23, 1920×1080, 30fps
- **对比图**：1920×1080，左半0°经线，右半180°经线，中间分隔线，顶部陆地/海洋百分比
- **旅程视频**：1920×1080，30fps，每帧显示经纬度坐标和海陆面积占比

## 算法说明

1. **椭圆检测**：通过质心法和径向扫描自动检测地图椭圆区域
2. **纹理提取**：从原图中裁剪并收缩椭圆（SHRINK=0.965），覆盖边缘坏像素
3. **3D渲染**：使用正交投影将纹理映射到球体，支持任意视角旋转
4. **颜色增强**：卫星云图风格——海洋深蓝、植被保留绿色、干旱/山地保留暖色调、云层亮白
5. **陆地/海洋统计**：基于蓝色通道占比判断像素类型

## 项目结构

```
mapbeauty/
├── globe.cpp              # 主程序源代码
├── CMakeLists.txt         # CMake 构建配置
├── fast_journey.py        # 旅程视频生成器（Python + C++混合管道）
├── build/                 # 构建目录
│   └── globe              # 可执行文件
├── test/                  # 测试图片目录
│   ├── test0.jpg ~ test7.jpg
├── video/                 # 视频素材目录
└── README.md              # 本文件
```