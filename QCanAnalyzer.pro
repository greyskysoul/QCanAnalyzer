QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000
DEFINES += ADS_STATIC

# ═══════════════════════════════════════════════════════════════
# 程序图标 (Windows 需 .ico 格式)
# ═══════════════════════════════════════════════════════════════
win32:RC_ICONS = icon.ico

# ═══════════════════════════════════════════════════════════════
# QHexEdit (静态编译)
# ═══════════════════════════════════════════════════════════════
QHEXEDIT_ROOT = libs/qhexedit2/src
INCLUDEPATH += $$QHEXEDIT_ROOT

# ═══════════════════════════════════════════════════════════════
# qt-advanced-docking-system (静态编译，无需预构建库)
# ═══════════════════════════════════════════════════════════════
ADS_ROOT = libs/Qt-Advanced-Docking-System
INCLUDEPATH += $$ADS_ROOT/src

# ═══════════════════════════════════════════════════════════════
# candle API (gs_usb / candleLight 驱动) — 仅 Windows
# ═══════════════════════════════════════════════════════════════
INCLUDEPATH += third_party/CandleApiDriver/api
INCLUDEPATH += third_party/pcan
INCLUDEPATH += third_party/zcanfd
INCLUDEPATH += third_party/zcan

win32 {
    SOURCES += \
        third_party/CandleApiDriver/api/candle.c \
        third_party/CandleApiDriver/api/candle_ctrl_req.c

    LIBS += -lwinusb -lsetupapi -lcfgmgr32 -lole32
}

SOURCES += \
    $$ADS_ROOT/src/ads_globals.cpp \
    $$ADS_ROOT/src/AutoHideDockContainer.cpp \
    $$ADS_ROOT/src/AutoHideSideBar.cpp \
    $$ADS_ROOT/src/AutoHideTab.cpp \
    $$ADS_ROOT/src/DockAreaTabBar.cpp \
    $$ADS_ROOT/src/DockAreaTitleBar.cpp \
    $$ADS_ROOT/src/DockAreaWidget.cpp \
    $$ADS_ROOT/src/DockComponentsFactory.cpp \
    $$ADS_ROOT/src/DockContainerWidget.cpp \
    $$ADS_ROOT/src/DockFocusController.cpp \
    $$ADS_ROOT/src/DockingStateReader.cpp \
    $$ADS_ROOT/src/DockManager.cpp \
    $$ADS_ROOT/src/DockOverlay.cpp \
    $$ADS_ROOT/src/DockSplitter.cpp \
    $$ADS_ROOT/src/DockWidget.cpp \
    $$ADS_ROOT/src/DockWidgetTab.cpp \
    $$ADS_ROOT/src/ElidingLabel.cpp \
    $$ADS_ROOT/src/FloatingDockContainer.cpp \
    $$ADS_ROOT/src/FloatingDragPreview.cpp \
    $$ADS_ROOT/src/IconProvider.cpp \
    $$ADS_ROOT/src/PushButton.cpp \
    $$ADS_ROOT/src/ResizeHandle.cpp

HEADERS += \
    $$ADS_ROOT/src/ads_globals.h \
    $$ADS_ROOT/src/AutoHideDockContainer.h \
    $$ADS_ROOT/src/AutoHideSideBar.h \
    $$ADS_ROOT/src/AutoHideTab.h \
    $$ADS_ROOT/src/DockAreaTabBar.h \
    $$ADS_ROOT/src/DockAreaTitleBar.h \
    $$ADS_ROOT/src/DockAreaTitleBar_p.h \
    $$ADS_ROOT/src/DockAreaWidget.h \
    $$ADS_ROOT/src/DockComponentsFactory.h \
    $$ADS_ROOT/src/DockContainerWidget.h \
    $$ADS_ROOT/src/DockFocusController.h \
    $$ADS_ROOT/src/DockingStateReader.h \
    $$ADS_ROOT/src/DockManager.h \
    $$ADS_ROOT/src/DockOverlay.h \
    $$ADS_ROOT/src/DockSplitter.h \
    $$ADS_ROOT/src/DockWidget.h \
    $$ADS_ROOT/src/DockWidgetTab.h \
    $$ADS_ROOT/src/ElidingLabel.h \
    $$ADS_ROOT/src/FloatingDockContainer.h \
    $$ADS_ROOT/src/FloatingDragPreview.h \
    $$ADS_ROOT/src/IconProvider.h \
    $$ADS_ROOT/src/PushButton.h \
    $$ADS_ROOT/src/ResizeHandle.h

RESOURCES += $$ADS_ROOT/src/ads.qrc \
    resources.qrc

# ═══════════════════════════════════════════════════════════════
# 源文件（通用）
# ═══════════════════════════════════════════════════════════════

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    can/canmanager.cpp \
    can/zcanfdadapter.cpp \
    ui/cansessionwidget.cpp \
    ui/welcomewidget.cpp \
    ui/sessionconfigdialog.cpp \
    $$QHEXEDIT_ROOT/qhexedit.cpp \
    $$QHEXEDIT_ROOT/chunks.cpp \
    $$QHEXEDIT_ROOT/commands.cpp \
    $$QHEXEDIT_ROOT/color_manager.cpp

HEADERS += \
    mainwindow.h \
    can/canmessage.h \
    can/caninterface.h \
    can/canmanager.h \
    can/zcanfdadapter.h \
    ui/cansessionwidget.h \
    ui/welcomewidget.h \
    ui/sessionconfigdialog.h \
    $$QHEXEDIT_ROOT/qhexedit.h \
    $$QHEXEDIT_ROOT/chunks.h \
    $$QHEXEDIT_ROOT/commands.h \
    $$QHEXEDIT_ROOT/color_manager.h

# ── Windows 适配器 ──
win32 {
    SOURCES += \
        can/pcanadapter.cpp \
        can/gsusbadapter.cpp \
        can/zcanadapter.cpp

    HEADERS += \
        can/pcanadapter.h \
        can/gsusbadapter.h \
        can/zcanadapter.h

    # ZCANFD: Windows 运行时需要 ControlCANFD.dll 放在 exe 同目录或 third_party/zcanfd/
    # ZCAN:   Windows 运行时需要 ControlCAN.dll 放在 exe 同目录或 third_party/zcan/
    # 此处仅声明依赖, 实际由 QLibrary 动态加载
    LIBS += -L$$PWD/third_party/zcanfd
    LIBS += -L$$PWD/third_party/zcan
}

# ── Linux 适配器 ──
unix:!macx {
    SOURCES += \
        can/socketcanadapter.cpp

    HEADERS += \
        can/socketcanadapter.h

    # ZCANFD: Linux 静态链接 libcontrolcanfd.a
    LIBS += -L$$PWD/third_party/zcanfd -lcontrolcanfd -lusb
}

# ── 虚拟适配器 (仅 Debug 模式) ──
CONFIG(debug, debug|release) {
    SOURCES += \
        can/mockcanadapter.cpp

    HEADERS += \
        can/mockcanadapter.h
}

FORMS += \
    mainwindow.ui \
    ui/welcomewidget.ui \
    ui/sessionconfigdialog.ui \
    ui/cansessionwidget.ui

TRANSLATIONS += \
    QCanAnalyzer_zh_CN.ts

# ═══════════════════════════════════════════════════════════════
# 部署规则
# ═══════════════════════════════════════════════════════════════

# Linux SocketCAN 需要 socket 库, ADS 需要 xcb 和 Qt 私有 QPA 头文件
unix:!macx {
    LIBS += -lxcb

    # Qt 私有 QPA 头文件路径 (ads_globals.cpp 需要 qpa/qplatformnativeinterface.h)
    QT += gui-private

    # Linux 平台特定的 ADS 源文件
    SOURCES += \
        $$ADS_ROOT/src/linux/FloatingWidgetTitleBar.cpp

    HEADERS += \
        $$ADS_ROOT/src/linux/FloatingWidgetTitleBar.h
}

# (socket 和 can 头文件已在 Linux 内核头文件中)

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 运行时需要 PCANBasic.dll 放在 exe 同目录
