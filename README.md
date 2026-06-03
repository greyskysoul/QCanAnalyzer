# QCanAnalyzer — CAN 总线调试分析工具

> ⚠️ **AI 声明**: 本项目 100% 由 GitHub Copilot (DeepSeek V4 Pro) 在 VS Code 中生成，包括但不限于：工程结构设计、PCAN/gs_usb/SocketCAN 多适配器架构、Qt Advanced Docking System 集成、所有 UI 布局与样式、CAN 报文收发逻辑。人工仅负责提出需求和编译验证。

---

## 功能特性

- 🔌 **多设备支持** — PCAN (PEAK USB/PCI)、gs_usb (candleLight)、SocketCAN (Linux)
- 🐧 **跨平台** — Windows + Linux，Linux 下原生支持 SocketCAN 和 gs_usb 内核驱动
- 🪟 **多会话停靠** — 基于 Qt Advanced Docking System，同时开启多个 CAN 会话，标签页分组
- 📡 **CAN-FD** — 新建会话时可配置仲裁域/数据域独立波特率
- 📥 **报文收发** — 标准帧/扩展帧/远程帧，支持周期发送
- 💾 **CSV 导出** — 一键保存所有收发帧为 CSV
- 🔍 **断线检测** — 500ms 间隔监控设备连接
- 🎨 **高 DPI 适配** — 175% 缩放正常

## 支持的设备

| 适配器 | 平台 | 说明 |
|--------|------|------|
| **PCAN** | Windows | PEAK-System 全系列，需 PCANBasic.dll |
| **gs_usb** | Windows / Linux | candleLight 等开源 CAN 适配器 |
| **SocketCAN** | Linux | 内核原生 CAN 子系统 (can0, vcan0...) |

### Linux 下 SocketCAN 使用注意

SocketCAN **不支持软件设置波特率**，请在连接前用 `ip` 命令配置：

```bash
sudo ip link set can0 type can bitrate 500000
sudo ip link set up can0
# 虚拟 CAN 用于测试:
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

## 截图

```
┌─────────────────────────────────────────┐
│  文件(F)  窗口(W)  帮助(H)              │
├─────────────────────────────────────────┤
│  ┌───────────┐                          │
│  │  icon.png │                          │
│  │           │                          │
│  │QCanAnalyzer│    ← 欢迎页            │
│  │   ...     │                          │
│  │[新建会话] │                          │
│  └───────────┘                          │
│  ┌─ PCAN_USB1 @ 500K ─────────────────┐│
│  │ 设备: PCAN_USB1  波特率:[500K ▼]  ││
│  │ 接收通道: ☑CH1                     ││
│  │ ┌─接收报文───────────────────────┐ ││
│  │ │ 时间     │ID   │类型│DLC│数据  │ ││
│  │ │ ...      │0x123│DATA│8  │AA BB│ │ ││
│  │ ├────────────────────────────────┤ ││
│  │ │ Rx:128 │☑自动滚动│[保存][清除]│ ││
│  │ └────────────────────────────────┘ ││
│  │ ┌─发送报文───────────────────────┐ ││
│  │ │ ID:[0x123] 类型:[标准] DLC:[8]│ ││
│  │ │ 数据:[00 11 22 33 44 55 66 77] │ ││
│  │ │ ☐周期发送 [1000ms]      [发送] │ ││
│  │ └────────────────────────────────┘ ││
│  └────────────────────────────────────┘│
└─────────────────────────────────────────┘
```

## 构建

### 环境要求

- **Qt 5.14+** (推荐 MinGW 64-bit / GCC)
- **Windows** 或 **Linux**
- Git

### 步骤

```bash
# 1. 克隆 ADS 库
cd libs
git clone https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
# 国内镜像: git clone https://gitee.com/czyt1988/Qt-Advanced-Docking-System.git
cd ..
```

**Windows — PCAN 设备**:
```bash
# 2. 安装 PCAN-Basic API
#    从 https://www.peak-system.com 下载安装
#    将 PCANBasic.dll 放到构建输出目录或 C:\Windows\System32\
```

**Windows — gs_usb 设备**:
```bash
# gs_usb (candleLight) 使用 candle API 静态编译，无需外部 DLL
# 驱动源文件位于 can/CandleApiDriver/api/ 目录
# 需要安装 WinUSB 驱动 (使用 Zadig 工具: https://zadig.akeo.ie/)
```

**Linux**:
```bash
# Linux 下 gs_usb 和 SocketCAN 均通过内核驱动，无需额外 DLL
# 确保加载了对应内核模块:
sudo modprobe gs_usb        # candleLight 设备
sudo modprobe can; sudo modprobe can_raw  # SocketCAN
```

```bash
# 3. 用 Qt Creator 打开 QCanAnalyzer.pro → 构建
```

## 项目结构

```
QCanAnalyzer/
├── QCanAnalyzer.pro           # Qt 工程
├── main.cpp                   # 入口 (高DPI + Fusion风格 + 图标)
├── mainwindow.h/.cpp/.ui      # 主窗口 (StackedWidget: 欢迎页 ↔ Dock区域)
├── icon.png / icon.ico        # 程序图标
├── can/
│   ├── canmessage.h           # CanMessage 数据结构
│   ├── caninterface.h         # CanInterface 抽象基类 + 波特率工具
│   ├── pcanadapter.h/.cpp     # PCAN 适配器 (动态加载 PCANBasic.dll)
│   ├── gsusbadapter.h/.cpp    # gs_usb 适配器 (candleLight)
│   ├── socketcanadapter.h/.cpp # SocketCAN 适配器 (Linux)
│   └── canmanager.h/.cpp      # 多会话管理器 (标签组管理)
├── ui/
│   ├── welcomewidget.h/.cpp   # 欢迎页
│   ├── sessionconfigdialog.h/.cpp # 新建会话对话框
│   └── cansessionwidget.h/.cpp # CAN 会话面板
└── libs/
    └── Qt-Advanced-Docking-System/  # (需自行克隆)
```

## 许可证

MIT
