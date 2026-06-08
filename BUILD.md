# QCanAnalyzer — CAN 总线调试分析工具

基于 Qt 5/6 + qt-advanced-docking-system 的多会话 CAN 调试工具，支持多种 CAN 设备。

## 功能特性

- 🪟 **多会话停靠框架** — 基于 qt-advanced-docking-system，同时打开多个 CAN 会话，自由拖拽布局
- 🔌 **多设备支持** — PCAN、gs_usb (candleLight)、ZCANFD (ZLG USBCANFD)、ZCAN (ZLG USBCAN)、SocketCAN (Linux)、MockCAN (虚拟)
- 📡 **CAN-FD 支持** — CAN FD 帧收发 (DLC 0~64)，ZCANFD 适配器原生支持
- 📥 **报文收发** — 接收 CAN 报文实时显示，支持周期/批量/指定帧数发送，发送中可随时打断
- 📊 **报文表格** — 时间戳、方向、ID、通道、类型、DLC、数据一目了然
- 🔍 **软过滤器** — ID 掩码过滤 + 通道使能复选框，灵活筛选报文
- 🧪 **MockCAN 虚拟适配器** — Debug 模式下自动可用，无需硬件即可测试

## 构建步骤

### 1. 准备工作

确保已安装:
- **Qt 5.15+** 或 **Qt 6.x** (需要 Qt Widgets 模块)
- **MSVC 2019+** 或 **MinGW 8.1+** (支持 C++17)
- **Git**

### 2. 克隆 qt-advanced-docking-system

```bash
cd libs
git clone https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
cd ..
```

> 如果你已有该库的其他位置，修改 `QCanAnalyzer.pro` 中的 `ADS_ROOT` 路径。

### 3. 准备设备驱动与 DLL

**所有设备的 DLL / .a 文件已通过 Git LFS 存放在 `third_party/` 下**，克隆后请确保 LFS 文件已拉取：
```bash
git lfs pull
```

**PCAN**: 仍需安装 [PEAK 驱动](https://www.peak-system.com)，`PCANBasic.dll` 已通过 LFS 存放。

**gs_usb (candleLight)**: 使用 WinUSB 驱动（[Zadig](https://zadig.akeo.ie/)），candle API 静态编译无需 DLL。

**ZCANFD / ZCAN**: DLL 已通过 LFS 存放，需安装 [ZLG USBCAN 驱动](https://www.zlg.cn)（随设备提供）。

> 程序运行时会自动动态加载对应的 DLL。

### 4. 构建

用 Qt Creator 打开 `QCanAnalyzer.pro`，或命令行构建:

```bash
mkdir build && cd build
qmake ../QCanAnalyzer.pro
make        # Linux/macOS
nmake       # MSVC
mingw32-make  # MinGW
```

### Linux 额外依赖

在 Linux 下需要安装 xcb 开发库:

```bash
# Debian/Ubuntu
sudo apt install libxcb1-dev libusb-1.0-0-dev

# Fedora
sudo dnf install libxcb-devel libusb1-devel

# Arch
sudo pacman -S libxcb libusb
```

Linux 下支持的适配器：
- **SocketCAN** — 内核原生，使用前用 `ip link` 配置波特率
- **ZCANFD** — 静态链接 `libcontrolcanfd.a`
- **gs_usb** — 加载 `gs_usb` 内核模块即可
```

### 5. 运行

```bash
./QCanAnalyzer    # Linux
QCanAnalyzer.exe  # Windows
```

## 项目结构

```
QCanAnalyzer/
├── QCanAnalyzer.pro          # Qt 工程文件
├── main.cpp                  # 入口
├── mainwindow.h / .cpp / .ui # 主窗口 (含 docking 框架)
├── can/
│   ├── canmessage.h          # CAN 消息数据结构 (CAN-FD 64字节, 通道号)
│   ├── caninterface.h        # CAN 接口抽象基类 + 波特率工具
│   ├── canmanager.h/.cpp     # 多会话管理器 (标签组管理)
│   ├── pcanadapter.h/.cpp    # PCAN 设备适配器 (动态加载 PCANBasic.dll)
│   ├── gsusbadapter.h/.cpp   # gs_usb 适配器 (candleLight, bittiming 自动搜索)
│   ├── zcanfdadapter.h/.cpp  # ZCANFD 适配器 (CAN FD, 多通道, 防重复打开)
│   ├── zcanadapter.h/.cpp    # ZCAN 适配器 (VCI API, 静态链接)
│   ├── socketcanadapter.h/.cpp # SocketCAN 适配器 (Linux, QSocketNotifier)
│   ├── mockcanadapter.h/.cpp # MockCAN 虚拟适配器 (Debug 模式, 随机报文)
│   └── CandleApiDriver/      # candle API 驱动
├── third_party/              # 第三方 SDK (Git LFS)
│   ├── pcan/PCANBasic.dll
│   ├── zcanfd/
│   └── zcan/
├── ui/
│   ├── welcomewidget.h/.cpp/.ui        # 欢迎页
│   ├── sessionconfigdialog.h/.cpp/.ui   # 新建会话对话框
│   └── cansessionwidget.h/.cpp/.ui     # CAN 会话面板 (收发/表格/软过滤/CSV导出)
├── libs/
│   └── Qt-Advanced-Docking-System/  # (需自行克隆)
└── pic/                      # 截图
```

## 扩展其他 CAN 设备

继承 `CanInterface` 并实现纯虚函数即可:

```cpp
class MyAdapter : public CanInterface {
    Q_OBJECT
public:
    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    QString adapterName() const override { return "MyDevice"; }
};
```

然后在 `CanSessionWidget` 中替换/添加适配器实例即可。
