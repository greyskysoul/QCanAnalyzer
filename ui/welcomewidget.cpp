#include "welcomewidget.h"
#include "ui_welcomewidget.h"
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QEvent>

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WelcomeWidget)
{
    ui->setupUi(this);
    setAutoFillBackground(false);

    qreal devicePixel = QApplication::primaryScreen()->devicePixelRatio();

    int iconSize = qMax(48, qRound(64 * devicePixel / 2));
    QPixmap icon(":/icon.png");
    ui->iconLabel->setPixmap(icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->iconLabel->setStyleSheet("background: transparent;");

    int titleSize = qMax(22, qRound(32 * devicePixel / 2));
    ui->titleLabel->setStyleSheet(
        QString("font-size: %1px; font-weight: bold; color: #2c3e50; background: transparent;")
            .arg(titleSize));

    int subSize = qMax(12, qRound(15 * devicePixel / 2));
    ui->subtitleLabel->setStyleSheet(
        QString("font-size: %1px; color: #7f8c8d; background: transparent; margin-bottom: 10px;")
            .arg(subSize));

    int lineWidth = qMax(120, qRound(200 * devicePixel / 2));
    ui->dividerLine->setFixedWidth(lineWidth);
    ui->dividerLine->setStyleSheet("background-color: #3498db; border-radius: 1px;");

    int descSize = qMax(11, qRound(13 * devicePixel / 2));
    ui->descLabel->setStyleSheet(
        QString("font-size: %1px; color: #95a5a6; background: transparent; "
                "padding: 10px 40px;").arg(descSize));

    int btnFont = qMax(13, qRound(15 * devicePixel / 2));
    int btnW = qMax(180, qRound(240 * devicePixel / 2));
    int btnH = qMax(38, qRound(48 * devicePixel / 2));
    ui->newSessionBtn->setMinimumSize(btnW, btnH);
    ui->newSessionBtn->setCursor(Qt::PointingHandCursor);
    ui->newSessionBtn->setStyleSheet(
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
    connect(ui->newSessionBtn, &QPushButton::clicked, this, &WelcomeWidget::newSessionRequested);

    int hintSize = qMax(10, qRound(12 * devicePixel / 2));
    ui->shortcutLabel->setStyleSheet(
        QString("font-size: %1px; color: #bdc3c7; background: transparent;")
            .arg(hintSize));
}

WelcomeWidget::~WelcomeWidget()
{
    delete ui;
}

void WelcomeWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    QWidget::changeEvent(event);
}

void WelcomeWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), QColor("#f8f9fa"));

    QLinearGradient grad(0, 0, width(), 0);
    grad.setColorAt(0.0, QColor("#3498db"));
    grad.setColorAt(0.5, QColor("#2980b9"));
    grad.setColorAt(1.0, QColor("#3498db"));
    painter.fillRect(QRect(0, 0, width(), 4), grad);

    QLinearGradient bottomGrad(0, height() - 60, 0, height());
    bottomGrad.setColorAt(0.0, QColor("#f8f9fa"));
    bottomGrad.setColorAt(1.0, QColor("#ecf0f1"));
    painter.fillRect(QRect(0, height() - 60, width(), 60), bottomGrad);
}
