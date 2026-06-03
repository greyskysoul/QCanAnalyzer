#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "can/canmanager.h"
#include "ui/welcomewidget.h"
#include "ui/sessionconfigdialog.h"

#include <DockManager.h>
#include <DockWidget.h>

#include <QMenuBar>
#include <QStatusBar>
#include <QAction>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QApplication>
#include <QScreen>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ─── StackedWidget: 页0=欢迎页, 页1=Dock区域 ───
    m_stack = new QStackedWidget();
    setCentralWidget(m_stack);

    // ── 页0: 欢迎页 ──
    auto *welcomePage = new QWidget();
    auto *wl = new QVBoxLayout(welcomePage);
    wl->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(welcomePage); // index 0

    // ── 页1: Dock 容器 ──
    auto *dockContainer = new QWidget();
    auto *dcl = new QVBoxLayout(dockContainer);
    dcl->setContentsMargins(0, 0, 0, 0);
    dcl->setSpacing(0);
    m_stack->addWidget(dockContainer); // index 1

    // ─── 初始化 qt-advanced-docking-system ───
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlCompressionEnabled, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    m_dockManager = new ads::CDockManager(dockContainer);
    // CDockManager 本身是 CDockContainerWidget，需手动加入布局
    dcl->addWidget(m_dockManager);

    // ─── CAN 会话管理器 ───
    m_canManager = new CanManager(m_dockManager, this);
    connect(m_canManager, &CanManager::allSessionsClosed,
            this, &MainWindow::onAllSessionsClosed);

    // ─── 菜单栏 ───
    setupMenuBar();

    // ─── 状态栏 ───
    setupStatusBar();

    // ─── 窗口基本属性 ───
    setWindowTitle("QCanAnalyzer - CAN Bus Debug Tool");
    qreal dpr = QApplication::primaryScreen()->devicePixelRatio();
    resize(static_cast<int>(1280 * dpr / 2), static_cast<int>(800 * dpr / 2));

    // ─── 显示欢迎页 ───
    showWelcomePage();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ═══════════════════════════════════════════════════════════════
// 欢迎页
// ═══════════════════════════════════════════════════════════════

void MainWindow::showWelcomePage()
{
    if (m_welcomeWidget) return;

    m_welcomeWidget = new WelcomeWidget();

    // 放进 stacked widget 的页0
    QWidget *page0 = m_stack->widget(0);
    // 先清空页0布局中旧控件
    if (auto *oldLayout = page0->layout()) {
        QLayoutItem *child;
        while ((child = oldLayout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }
    }
    page0->layout()->addWidget(m_welcomeWidget);

    m_stack->setCurrentIndex(0);

    connect(m_welcomeWidget, &WelcomeWidget::newSessionRequested,
            this, &MainWindow::onNewSession);
}

void MainWindow::hideWelcomePage()
{
    if (!m_welcomeWidget) return;

    m_welcomeWidget->deleteLater();
    m_welcomeWidget = nullptr;

    m_stack->setCurrentIndex(1);
}

// ═══════════════════════════════════════════════════════════════
// 菜单栏
// ═══════════════════════════════════════════════════════════════

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("文件(&F)");

    QAction *newSessionAct = new QAction("新建会话(&N)", this);
    newSessionAct->setShortcut(QKeySequence("Ctrl+N"));
    connect(newSessionAct, &QAction::triggered, this, &MainWindow::onNewSession);
    fileMenu->addAction(newSessionAct);

    QAction *closeAllAct = new QAction("关闭所有会话", this);
    connect(closeAllAct, &QAction::triggered, this, &MainWindow::onCloseAllSessions);
    fileMenu->addAction(closeAllAct);

    fileMenu->addSeparator();

    QAction *exitAct = new QAction("退出(&X)", this);
    exitAct->setShortcut(QKeySequence("Alt+F4"));
    connect(exitAct, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(exitAct);

    QMenu *helpMenu = menuBar()->addMenu("帮助(&H)");
    QAction *aboutAct = new QAction("关于(&A)", this);
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "关于 QCanAnalyzer",
            "<h3>QCanAnalyzer v1.0</h3>"
            "<p>基于 Qt 的 CAN 总线调试分析工具</p>"
            "<p>支持 PCAN 设备  |  多会话同时工作</p>");
    });
    helpMenu->addAction(aboutAct);
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage("就绪  —  按 Ctrl+N 新建 CAN 会话");
}

// ═══════════════════════════════════════════════════════════════
// 槽
// ═══════════════════════════════════════════════════════════════

void MainWindow::onNewSession()
{
    // 弹出配置对话框
    SessionConfigDialog dlg(this);
    int channel = 0;
    CanBaudRate baud = CanBaudRate::BR_500K;
    bool isCanFd = false;

    if (!dlg.configure(channel, baud, isCanFd))
        return; // 用户取消

    // 首次创建会话时隐藏欢迎页
    if (!m_canManager->hasSessions())
        hideWelcomePage();

    m_canManager->createSession(channel, baud, isCanFd);
    statusBar()->showMessage(
        QString("已创建会话 — 当前共 %1 个会话").arg(m_canManager->sessionCount()), 3000);
}

void MainWindow::onCloseAllSessions()
{
    QList<CanSessionWidget*> sessions = m_canManager->sessions();
    for (auto *s : sessions)
        m_canManager->closeSession(s->sessionId());
    statusBar()->showMessage("所有会话已关闭", 3000);
}

void MainWindow::onAllSessionsClosed()
{
    showWelcomePage();
    statusBar()->showMessage("就绪  —  按 Ctrl+N 新建 CAN 会话");
}

