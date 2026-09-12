# QCanAnalyzer — CAN 总线调试分析工具

基于 Qt 5.15 + qt-advanced-docking-system 的多会话 CAN 调试工具，支持多种 CAN 设备。

> 工具链限制：目前**仅支持 MinGW / GCC**。仓库内的 ZCAN/ZCANFD 只有 MinGW 导入库（`lib*.a`），
> `nmake`/MSVC 链接会失败。macOS 也不在支持范围（`.pro` 只处理 `win32` 与 `unix:!macx`）。

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
- **Qt 5.15.x**（CI 验证版本为 5.15.2；仓库无 Qt6 构建矩阵）
- **MinGW 8.1+**（支持 C++17）或 Linux 下的 GCC
- **Git + Git LFS**

### 2. 克隆第三方依赖

`libs/` 被 `.gitignore` 整体忽略，不会随克隆出现，必须手动准备：

```bash
mkdir -p libs && cd libs
git clone --depth 1 --branch 4.5.0 https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
git clone --depth 1 https://github.com/Simsys/qhexedit2.git
cd ..
```

> ADS 必须用 tag **4.5.0**：`QCanAnalyzer.pro` 中的源文件清单是按该版本写死的，master 不匹配。
> 若使用了其他位置的 ADS/qhexedit2，可用 `qmake "ADS_ROOT=/path" ...` 覆盖 `.pro` 中的默认路径。

ADS 4.5.0 的 `ads_version.h` 由上游 CMake 生成，qmake 构建下需手工提供：

```bash
cp ci/ads_version.h libs/Qt-Advanced-Docking-System/src/ads_version.h
```

### 3. 准备设备驱动与 DLL

**所有设备的 DLL / .a 文件已通过 Git LFS 存放在 `third_party/` 下**，克隆后请确保 LFS 文件已拉取：
```bash
git lfs pull
```

**PCAN**: 仍需安装 [PEAK 驱动](https://www.peak-system.com)，`PCANBasic.dll` 已通过 LFS 存放。

**gs_usb (candleLight)**: 使用 WinUSB 驱动（[Zadig](https://zadig.akeo.ie/)），candle API 静态编译无需 DLL。

**ZCANFD / ZCAN**: DLL 已通过 LFS 存放，需安装 [ZLG USBCAN 驱动](https://www.zlg.cn)（随设备提供）。

> **PCAN** 在运行时通过 `QLibrary` 动态加载 `PCANBasic.dll`；ZCAN / ZCANFD 则是编译期链接导入库，
> DLL 需位于 exe 同目录或系统搜索路径。

### 4. 构建

用 Qt Creator 打开 `QCanAnalyzer.pro`，或命令行构建:

```bash
mkdir build && cd build
qmake "CONFIG+=release" ../QCanAnalyzer.pro
mingw32-make -j          # Windows / MinGW
make -j$(nproc)          # Linux / GCC
```

产物：MinGW 在 `build/release/QCanAnalyzer.exe`，Linux 在 `build/QCanAnalyzer`。
不传 `CONFIG+=release` 时为 Debug 构建，此时会额外编译 `can/mockcanadapter.cpp`（虚拟适配器）。

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
- **ZCANFD** — 静态链接 `libControlCANFD.a`
- **gs_usb** — 加载 `gs_usb` 内核模块即可

> Windows 下 `build/release/` 中的 exe 需要 `PCANBasic.dll`、`ControlCAN.dll`、`ControlCANFD.dll`
> 以及 MinGW 运行库（`libgcc_s_seh-1.dll` 等），可用 `windeployqt` 补齐 Qt 部分。

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
│   ├── pcanadapter.h/.cpp    # PCAN 设备适配器 (运行时加载 PCANBasic.dll)
│   ├── gsusbadapter.h/.cpp   # gs_usb 适配器 (candleLight, 精确 bittiming 搜索)
│   ├── zcanfdadapter.h/.cpp  # ZCANFD 适配器 (CAN FD, 多通道, 防重复打开)
│   ├── zcanadapter.h/.cpp    # ZCAN 适配器 (VCI API, 静态链接)
│   ├── socketcanadapter.h/.cpp # SocketCAN 适配器 (Linux, QSocketNotifier)
│   └── mockcanadapter.h/.cpp # MockCAN 虚拟适配器 (仅 Debug 编译)
├── ci/
│   └── ads_version.h         # 供 qmake 构建补充到 ADS 源码目录
├── third_party/              # 第三方 SDK
│   ├── CandleApiDriver/      # candle API (编译进可执行文件)
│   ├── pcan/PCANBasic.dll    # Git LFS
│   ├── zcanfd/               # ZCANFD SDK + 导入库 (Git LFS)
│   └── zcan/                 # ZCAN (VCI) SDK + 导入库 (Git LFS)
├── ui/
│   ├── welcomewidget.h/.cpp/.ui        # 欢迎页
│   ├── sessionconfigdialog.h/.cpp/.ui   # 新建会话对话框
│   └── cansessionwidget.h/.cpp/.ui     # CAN 会话面板 (收发/表格/软过滤/CSV导出)
├── libs/                     # 需自行克隆，不入库
│   ├── Qt-Advanced-Docking-System/  # tag 4.5.0
│   └── qhexedit2/                   # 发送区十六进制编辑器
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
