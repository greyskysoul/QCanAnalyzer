# AGENTS.md

Qt/C++ 多会话 CAN / CAN-FD 总线调试分析工具。功能特性、支持设备与目录结构见 [README.md](README.md)；依赖与平台细节见 [BUILD.md](BUILD.md)。

## 构建

首次构建前必须先准备 `libs/`——它被 `.gitignore` 整体忽略，不会随克隆出现在工作区（ADS 需固定 tag 4.5.0，另需 qhexedit2 与 ADS 缺失的 `ads_version.h`）：

```bash
mkdir -p libs && cd libs
git clone --depth 1 --branch 4.5.0 https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
git clone --depth 1 https://github.com/Simsys/qhexedit2.git
cd ..
git lfs pull
# ADS 4.5.0 的 ads_version.h 由 CMake 生成，qmake 构建下必须手工提供：
cp ci/ads_version.h libs/Qt-Advanced-Docking-System/src/ads_version.h
```

```bash
mkdir -p build && cd build
qmake "CONFIG+=release" ../QCanAnalyzer.pro
mingw32-make -j        # Windows/MinGW；Linux 用 make -j$(nproc)
```

- 产物：MinGW 在 `build/release/QCanAnalyzer.exe`，Linux/GCC 在 `build/QCanAnalyzer`。
- **只支持 MinGW/GCC**。ZCANFD/ZCAN 的 `LIBS` 是 `-lControlCANFD_win` 这类写法，而仓库内只有 MinGW 导入库（`lib*.a`），`nmake`/MSVC 链接必然失败——`BUILD.md` 中「MSVC 2019+」不可用。macOS 不在支持范围（`.pro` 只处理 `win32` 与 `unix:!macx`）。
- `CONFIG(debug, ...)` 才编译 `can/mockcanadapter.cpp`（无硬件测试用）；Release 构建**不含** MockCAN。
- CI（[.github/workflows/build.yml](.github/workflows/build.yml)）只验证 Qt 5.15.2 + MinGW/GCC，**没有 Qt6 构建**。

## 架构

```
MainWindow (QStackedWidget: 欢迎页 / CDockManager)
└── CanManager                 // 只管理会话与 dock 标签组，不持有适配器
    └── CanSessionWidget × N   // 每个会话独占一个适配器（以 widget 为 parent）
        └── CanInterface 子类：PcanAdapter / GsUsbAdapter / ZcanFdAdapter /
                               ZcanAdapter / SocketCanAdapter / MockCanAdapter
```

- `can/caninterface.h` 是唯一的抽象点，子类必须实现 `scanDevices / open / close / isOpen / sendMessage / adapterName`；可选覆写 `isAlive / availableSendChannels / setSendChannel / currentSendChannel`。
- 适配器只用两个信号上报：`messageReceived(CanMessage)` 与 `errorOccurred(QString)`。**没有异常、没有错误码约定**（仅 `sendMessage` 返回 `bool`）。
- **`CanMessage::dlc` 一律是数据字节数**，而各厂商 SDK 的 DLC 字段往往是编码值（0~15）。收包用 `canFdDlcToLen()` 归一化成字节数，发包用 `canFdLenToDlc()` 转回编码；FD 帧的字节数只能是 0~8/12/16/20/24/32/48/64。
- **波特率分仲裁域与数据域**：`open()` 的 `baud` 是仲裁域（`CanBaudRate`，值即 PCAN 编码），`dataBaud` 是数据域（`CanDataBaudRate`，值即 Hz）。`CanDataBaudRate::None` 表示经典 CAN，同时充当 CAN-FD 开关。只有 ZCANFD 与 gs_usb 会真正配置数据域；PCAN/ZCAN 是经典 CAN 适配器、SocketCAN 由内核按 `ip link` 配置，三者按设计忽略该参数。
- `CanSessionWidget` **不反向依赖** `CanManager`；适配器由它在 `connectDevice()` 中惰性 `new` 并缓存，切换类型时复用实例。
- 收包全部在 GUI 线程：轮询用 `QTimer`（PCAN/ZCAN 1ms、gs_usb 2ms），只有 SocketCAN 用 `QSocketNotifier`。**没有接收线程、没有互斥锁**——不要假设存在后台线程，也不要在 GUI 线程引入阻塞调用（现存唯一例外是 `gsusbadapter.cpp` Bus-Off 恢复中的 `QThread::msleep(10)`）。
- 断线检测：`CanSessionWidget` 用 500ms 定时器轮询 `isAlive()`。只有 gs_usb（设备时间戳）与 PCAN（`CAN_GetStatus`）真正探测硬件，ZCAN/ZCANFD/SocketCAN 是 `return m_opened`，拔线不会被发现。

## 约定

- **UI 文本必须 `tr()` 或写进 `.ui`**。代码里动态设置/会被 `retranslateUi()` 重置的文本（状态标签、`setSpecialValueText`、`setSuffix` 等），要在该 widget 的 `changeEvent(QEvent::LanguageChange)` 里重新设置——见 `CanSessionWidget::retranslateDynamicUi()`。
- **改完翻译必须重新生成 `.qm` 并提交**：`lupdate QCanAnalyzer.pro` → 编辑 `.ts` → `lrelease QCanAnalyzer.pro` → `git add -f *.qm`（`.gitignore` 忽略 `*.qm`，但两个 `.qm` 是被跟踪的，CI 只拷贝根目录产物、**从不运行 lrelease**）。`QCanAnalyzer_zh_CN.ts` 全为 `type="unfinished"`：源字符串本身即中文，界面靠回退到 source 显示，这是预期状态，不要「修好」它。
- **`.ui` 只放静态骨架**：全仓库 `.ui` 的 `<connections/>` 均为空，信号连接、动态条目、逐控件样式都在 C++ 中。不要新增 `<connections>`；槽名用 `onXxxClicked()` 风格（刻意避开 `on_<object>_<signal>` 自动连接）。
- **新增/删除源文件必须同步 `QCanAnalyzer.pro`**：没有 CMake、没有通配符、没有构建脚本。
- **平台守卫**：C++ 中用 `#ifndef Q_OS_LINUX`（非 Linux 分支）/`#ifdef Q_OS_LINUX`，Debug 用 `#ifdef QT_DEBUG`，与 `.pro` 的 `win32` / `unix:!macx` / `CONFIG(debug, ...)` 块保持一致。
- 命名与风格：成员 `m_camelCase`，4 空格缩进，头文件 `#ifndef XXX_H` 保护。无 `.clang-format` / lint 配置。
- 样式分散在各 widget 的 `setStyleSheet()`；`style.qss`（编译进资源）只管 `QCheckBox` 指示器，改它收益有限。
- `third_party/` 是原样引入的厂商 SDK/驱动，**不要修改**；`libs/` 是需手工克隆的上游依赖，同样不入库。

## 常见陷阱

- **`CanAdapterType::MockCan` 被 `#ifdef QT_DEBUG` 包裹**：Release 下该枚举值不存在，引用处必须加同样守卫。新增适配器枚举值只能**追加在 `MockCan` 之前**；改动已有数值会破坏 `adapterType` 的整数含义。
- `CanDeviceInfo::channel` 是**复合编码**：高字节=设备索引，低字节=通道号（`(devIdx << 8) | ch`）。**例外是 PCAN**，它用 16 位硬件 handle，`CanSessionWidget` 取低 4 位。
- **ZCAN/ZCANFD 的设备扫描结果被静态缓存**：已有会话打开时 `scanDevices()` 不再真正扫描，只从缓存中剔除已占用设备（重复扫描会让在用设备发送失败）。因此「检测不到设备」通常意味着还有 ZCAN 会话没关。
- `.pro` 中关于 ZCANFD「由 `QLibrary` 动态加载」的注释与实现不符，**实际是静态 API 直接调用**（只有 PCAN 用 `QLibrary`）。以代码为准。
- 高码率收包路径是明确的性能风险点：适配器逐帧 `emit`，`onMessageReceived` 同步 `insertRow`，表格上限 5000 行，通道使能与软过滤都是逐条线性扫描，**没有批量/节流刷新**。优化前先定位真实瓶颈（另见文末「已知设计问题」）。
- 文档与实现曾多处不一致，已在 2026-09 校正 `README.md` / `BUILD.md`；若再发现偏差，**以代码和 `.pro` 为准**。

## 新增一个 CAN 适配器

六个注册点，缺一不可：

1. `can/<name>adapter.h/.cpp` — 继承 `CanInterface`，实现 6 个纯虚函数；`open(channel, baud, dataBaud)` 中 `dataBaud == CanDataBaudRate::None` 表示经典 CAN。`scanDevices()` 填 `CanDeviceInfo` 时按复合编码写 `channel`。
2. `can/caninterface.h` — 在 `CanAdapterType` 追加枚举值（`MockCan` 之前）。
3. `QCanAnalyzer.pro` — 加入 `SOURCES`/`HEADERS`；平台专属代码放进对应的 `win32` / `unix:!macx` 块。
4. `ui/sessionconfigdialog.cpp` — 适配器下拉框 `addItem()` + `scanDevices()` 的 switch（按类型创建栈上临时适配器，只调 `scanDevices`）。
5. `ui/cansessionwidget.h/.cpp` — 成员指针 + `connectDevice()` 的 switch；新实例必须经 `linkSignals()` 连接信号，不要重复连接。
6. `can/canmanager.cpp` — 会话标签标题的 switch（`xxxAdapter::channelName(channel)`）。

## 热点文件

| 文件 | 为什么重要 |
| --- | --- |
| `ui/cansessionwidget.cpp` | 功能主体：收发、报文表格、软过滤、周期/批量发送、CSV 导出 |
| `can/caninterface.h` | 适配器抽象基类、`CanAdapterType`/`CanBaudRate` 枚举、波特率字符串互转 |
| `can/canmessage.h` | `CanMessage` 与 `canFdDlcToLen` / `canFdLenToDlc`（DLC 编解码互转） |
| `can/canmanager.cpp` | 会话 / dock 标签组生命周期（`m_closingSessions` 防重入，统一 `deleteLater`） |
| `ui/sessionconfigdialog.cpp` | 新建会话对话框；全项目唯一做设备扫描的 UI |
| `main.cpp` | 高 DPI、Fusion 主题、`--lang` 参数、加载全局 qss |

## 已知设计问题（待修）

- **SocketCAN 的 `msg.channel` 恒为 0**：`scanDevices()` 把接口下标写进 `CanDeviceInfo::channel`，但回包统一标 0，多接口会话无法区分来源。
