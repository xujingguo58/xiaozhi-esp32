# Livis macOS 模拟器

模拟器直接编译 `main/boards/lckfb/szpi-esp32s3/livis_display.h` 和 `livis_pages.h`，预览 320 × 240、16 位颜色的表情与待机页面。模拟器中的 LCD 基类、Wi-Fi 和设备状态是桌面替身，天气、股票及 Codex 用量均为 `main.cpp` 中的示例数据，时钟使用 Mac 本地时间。

需要 CMake、SDL2、FreeType（可通过 `brew install freetype` 安装），以及 ESP-IDF 组件管理器下载的 `managed_components/lvgl__lvgl` 和 `managed_components/78__xiaozhi-fonts`。本机 SDL2 安装在 `~/.local/speaker-sdl2`：

```sh
cmake -S simulator -B simulator/build -DCMAKE_PREFIX_PATH="$HOME/.local/speaker-sdl2"
cmake --build simulator/build -j
./simulator/build/speaker_simulator
```

启动后等待 4 秒进入天气页。窗口获得焦点时：

- 空格：切换天气、车辆、行情、Codex 用量页面。模拟器车辆页使用固定示例数据，不连接车辆。
- 1：待机（4 秒后返回天气页）。
- 2：聆听；3：思考；4：说话；5：连接中。

修改真实界面文件后重新编译模拟器即可预览；修改示例数据请编辑 `simulator/main.cpp`。运行 `./simulator/build/speaker_simulator --self-test` 可检查页面和状态切换。

首页室内温湿度为模拟器示例，不连接米家。`--font-check` 检查室内控件在固件专用字库中的字形；`--screenshot /tmp/livis.bmp` 将 LVGL 实际绘制结果保存为 BMP。室内字体使用 LVGL 附带的 Source Han Sans SC 子集（OFL，许可见板级目录的 `livis_indoor_font_OFL.txt`），与 macOS 补字回退无关。

`--screenshot-vehicle /tmp/livis-vehicle.bmp` 截取车辆页；车辆页使用与固件相同的专用中文字库。

桌面基类仅模拟界面所需的部分 LCD 行为，字体优先使用固件内置字库，缺失的字形由 macOS 的苹方字体通过 FreeType 补充；模拟器使用 macOS 字体补齐缺字，字形和间距可能与真机不同。硬件、网络、实际数据源、定时器调度和整机性能仍需真机验证。
