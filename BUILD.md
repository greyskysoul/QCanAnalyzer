# QCanAnalyzer — CAN 总线调试分析工具

基于 Qt 5/6 + qt-advanced-docking-system 的多会话 CAN 调试工具，兼容 PCAN 设备。

## 功能特性

- 🪟 **多会话停靠框架** — 基于 qt-advanced-docking-system，同时打开多个 CAN 会话，自由拖拽布局
- 🔌 **PCAN 设备兼容** — 动态加载 PCANBasic.dll，支持 USB/PCI/ISA 全系列 PEAK CAN 设备
- 📥 **报文收发** — 接收 CAN 报文实时显示，支持周期发送
- 📊 **报文表格** — 时间戳、ID、类型、DLC、数据一目了然

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

### 3. 准备 PCAN Basic API

1. 从 [PEAK-System 官网](https://www.peak-system.com/PCAN-Basic.239.0.html) 下载 **PCAN-Basic API**
2. 安装后将 `PCANBasic.dll` 复制到以下位置之一：
   - 项目构建输出目录 (如 `build/Desktop_Qt_.../debug/`)
   - `C:\Windows\System32\`
   - 或添加到系统 PATH

> 仅 Windows 需要。程序运行时会自动动态加载此 DLL。

### 4. 构建

用 Qt Creator 打开 `QCanAnalyzer.pro`，或命令行构建:

```bash
mkdir build && cd build
qmake ../QCanAnalyzer.pro
make        # Linux/macOS
nmake       # MSVC
mingw32-make  # MinGW
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
│   ├── canmessage.h          # CAN 消息数据结构
│   ├── caninterface.h        # CAN 接口抽象基类
│   ├── pcanadapter.h/.cpp    # PCAN 设备适配器
│   └── canmanager.h/.cpp     # 多会话管理器
├── ui/
│   └── cansessionwidget.h/.cpp # CAN 会话面板 (停靠窗口内容)
└── libs/
    └── Qt-Advanced-Docking-System/  # (需自行克隆)
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
