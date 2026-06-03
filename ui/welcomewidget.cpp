#include "welcomewidget.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QWindow>
#include <QPixmap>

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(600, 400);
    setAutoFillBackground(false);

    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(18);

    qreal devicePixel = QApplication::primaryScreen()->devicePixelRatio();

    // ── 图标区域 ──
    {
        int iconSize = qMax(48, qRound(64 * devicePixel / 2));
        auto *iconLabel = new QLabel();
        iconLabel->setAlignment(Qt::AlignCenter);
        QPixmap icon(":/icon.png");
        iconLabel->setPixmap(icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setStyleSheet("background: transparent;");
        layout->addWidget(iconLabel);
    }

    // ── 标题 ──
    {
        int titleSize = qMax(22, qRound(32 * devicePixel / 2));
        auto *titleLabel = new QLabel("QCanAnalyzer");
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setStyleSheet(
            QString("font-size: %1px; font-weight: bold; color: #2c3e50; background: transparent;")
                .arg(titleSize));
        layout->addWidget(titleLabel);
    }

    // ── 副标题 ──
    {
        int subSize = qMax(12, qRound(15 * devicePixel / 2));
        auto *subtitleLabel = new QLabel("CAN 总线调试分析工具");
        subtitleLabel->setAlignment(Qt::AlignCenter);
        subtitleLabel->setStyleSheet(
            QString("font-size: %1px; color: #7f8c8d; background: transparent; margin-bottom: 10px;")
                .arg(subSize));
        layout->addWidget(subtitleLabel);
    }

    // ── 分割线 ──
    {
        int lineWidth = qRound(200 * devicePixel / 2);
        auto *line = new QWidget();
        line->setFixedSize(qMax(120, lineWidth), 2);
        line->setStyleSheet("background-color: #3498db; border-radius: 1px;");
        layout->addWidget(line, 0, Qt::AlignCenter);
    }

    // ── 描述文字 ──
    {
        int descSize = qMax(11, qRound(13 * devicePixel / 2));
        auto *descLabel = new QLabel(
            "支持 PCAN 系列设备  ·  多会话同时工作  ·  报文实时分析");
        descLabel->setAlignment(Qt::AlignCenter);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet(
            QString("font-size: %1px; color: #95a5a6; background: transparent; "
                    "padding: 10px 40px;").arg(descSize));
        descLabel->setMaximumWidth(500);
        layout->addWidget(descLabel);
    }

    // ── 新建会话按钮 ──
    {
        int btnFont = qMax(13, qRound(15 * devicePixel / 2));
        int btnW = qMax(180, qRound(240 * devicePixel / 2));
        int btnH = qMax(38, qRound(48 * devicePixel / 2));
        m_newSessionBtn = new QPushButton("＋ 新建 CAN 会话");
        m_newSessionBtn->setMinimumSize(btnW, btnH);
        m_newSessionBtn->setCursor(Qt::PointingHandCursor);
        m_newSessionBtn->setStyleSheet(
            QString(
                "QPushButton {"
                "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                "    stop:0 #3498db, stop:1 #2980b9);"
                "  color: white;"
                "  font-size: %1px;"
                "  font-weight: bold;"
                "  border: none;"
                "  border-radius: %2px;"
                "  padding: 0px 24px;"
                "}"
                "QPushButton:hover {"
                "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                "    stop:0 #2980b9, stop:1 #1a5276);"
                "}"
                "QPushButton:pressed {"
                "  background: #1a5276;"
                "}"
            ).arg(btnFont).arg(btnH / 2));
        connect(m_newSessionBtn, &QPushButton::clicked, this, &WelcomeWidget::newSessionRequested);
        layout->addWidget(m_newSessionBtn, 0, Qt::AlignCenter);
    }

    layout->addSpacing(10);

    // ── 快捷键提示 ──
    {
        int hintSize = qMax(10, qRound(12 * devicePixel / 2));
        auto *shortcutLabel = new QLabel("提示: 按 Ctrl+N 快速新建会话");
        shortcutLabel->setAlignment(Qt::AlignCenter);
        shortcutLabel->setStyleSheet(
            QString("font-size: %1px; color: #bdc3c7; background: transparent;")
                .arg(hintSize));
        layout->addWidget(shortcutLabel);
    }

    // ── 底部版本 ──
    {
        int verSize = qMax(9, qRound(11 * devicePixel / 2));
        auto *versionLabel = new QLabel("v1.0  |  Qt 5.14  |  PCAN");
        versionLabel->setAlignment(Qt::AlignCenter);
        versionLabel->setStyleSheet(
            QString("font-size: %1px; color: #bdc3c7; background: transparent; margin-top: 20px;")
                .arg(verSize));
        layout->addWidget(versionLabel);
    }
}

void WelcomeWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 白色背景
    painter.fillRect(rect(), QColor("#f8f9fa"));

    // 顶部蓝色渐变条
    QLinearGradient grad(0, 0, width(), 0);
    grad.setColorAt(0.0, QColor("#3498db"));
    grad.setColorAt(0.5, QColor("#2980b9"));
    grad.setColorAt(1.0, QColor("#3498db"));
    painter.fillRect(QRect(0, 0, width(), 4), grad);

    // 绘制装饰性底部渐变
    QLinearGradient bottomGrad(0, height() - 60, 0, height());
    bottomGrad.setColorAt(0.0, QColor("#f8f9fa"));
    bottomGrad.setColorAt(1.0, QColor("#ecf0f1"));
    painter.fillRect(QRect(0, height() - 60, width(), 60), bottomGrad);
}
