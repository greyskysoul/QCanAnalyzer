#include "cansessionwidget.h"
#include "ui_cansessionwidget.h"
#ifndef Q_OS_LINUX
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#else
#include "can/socketcanadapter.h"
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QApplication>
#include <QScreen>
#include <QFileDialog>
#include <QTextStream>
#include <QDateTime>
#include <QFont>

CanSessionWidget::CanSessionWidget(int sessionId, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CanSessionWidget)
    , m_sessionId(sessionId)
{
    ui->setupUi(this);
    setupUi();

#ifndef Q_OS_LINUX
    m_pcan = new PcanAdapter(this);
    m_can = m_pcan;
    linkSignals(m_pcan);
#else
    m_socketcan = new SocketCanAdapter(this);
    m_can = m_socketcan;
    linkSignals(m_socketcan);
#endif

    m_frameTimer = new QTimer(this);
    m_frameTimer->setSingleShot(false);
    connect(m_frameTimer, &QTimer::timeout, this, &CanSessionWidget::onSendOneFrame);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &CanSessionWidget::onStatusCheck);

    setWindowTitle(QString("CAN Session %1").arg(sessionId));
}

CanSessionWidget::~CanSessionWidget()
{
    // 析构时直接清理资源，不调用 disconnectDevice()
    // 因为 disconnectDevice() 会访问 UI 控件，而此时 UI 可能已部分销毁
    m_statusTimer->stop();
    m_frameTimer->stop();
    if (m_can) {
        m_can->close();
    }
    delete ui;
}

void CanSessionWidget::linkSignals(CanInterface *iface)
{
    connect(iface, &CanInterface::messageReceived,
            this, &CanSessionWidget::onMessageReceived);
    connect(iface, &CanInterface::errorOccurred, this, [this](const QString &err) {
        QString shortErr = err;
        if (shortErr.length() > 50)
            shortErr = shortErr.left(47) + "...";
        ui->statusLabel->setText("⚠ " + shortErr);
        ui->statusLabel->setToolTip(err);
        ui->statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });
}

void CanSessionWidget::setupUi()
{
    qreal scale = QApplication::primaryScreen()->devicePixelRatio();

    const QString btnStyle = QStringLiteral(
        "QPushButton { font-weight: bold; border-radius: 3px; padding: 4px 10px; }");
    const QString greenBtn = btnStyle +
        "QPushButton { background-color: #4CAF50; color: white; }"
        "QPushButton:hover { background-color: #45a049; }";
    const QString grayBtn = btnStyle +
        "QPushButton { background-color: #607d8b; color: white; }"
        "QPushButton:hover { background-color: #455a64; }";

    // ─── 连接控制栏 ───
    ui->deviceLabel->setStyleSheet("font-weight:bold; color:#2c3e50;");
    ui->deviceLabel->setMinimumWidth(qRound(120 * scale));

    ui->baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    ui->baudCombo->setCurrentText("500K");

    ui->connectBtn->setFixedWidth(qRound(70 * scale));
    ui->connectBtn->setStyleSheet(greenBtn);
    connect(ui->connectBtn, &QPushButton::clicked, this, &CanSessionWidget::onConnectClicked);

    ui->statusLabel->setStyleSheet("color:gray; font-weight:bold;");
    ui->statusLabel->setMinimumWidth(qRound(120 * scale));

    // ─── 接收表格 ───
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColTime, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColTime, qRound(130 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColId, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColId, qRound(90 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColCh, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColCh, qRound(45 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColType, qRound(55 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColDlc, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColDlc, qRound(45 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColData, QHeaderView::Stretch);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColDir, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColDir, qRound(50 * scale));
    ui->rxTable->horizontalHeader()->setStretchLastSection(false);
    ui->rxTable->verticalHeader()->setDefaultSectionSize(qRound(24 * scale));

    // ─── 接收状态栏 ───
    ui->saveBtn->setFixedWidth(qRound(55 * scale));
    ui->saveBtn->setStyleSheet(grayBtn);
    connect(ui->saveBtn, &QPushButton::clicked, this, &CanSessionWidget::onSaveClicked);

    ui->clearBtn->setFixedWidth(qRound(55 * scale));
    ui->clearBtn->setStyleSheet(grayBtn);
    connect(ui->clearBtn, &QPushButton::clicked, this, &CanSessionWidget::onClearClicked);

    // ─── 发送面板 ───
    ui->sendIdEdit->setMaximumWidth(qRound(100 * scale));
    ui->sendTypeCombo->addItems({"标准数据帧", "扩展数据帧", "远程帧"});
    ui->sendDlcSpin->setRange(0, 64);
    ui->sendDlcSpin->setValue(8);
    ui->sendDlcSpin->setFixedWidth(qRound(55 * scale));

    // 数据输入 — QPlainTextEdit 放在最下面，方便输入 CAN FD 数据
    ui->sendDataEdit->setFixedHeight(qRound(56 * scale));
    ui->sendDataEdit->setTabChangesFocus(true);

    // 帧间隔 SpinBox：0 = 最快速，>0 = 每帧间隔 N ms
    ui->sendPeriodSpin->setRange(0, 10000);
    ui->sendPeriodSpin->setValue(0);
    ui->sendPeriodSpin->setSpecialValueText("最快");
    ui->sendPeriodSpin->setSuffix(" ms");

    // 帧数 SpinBox
    ui->sendFrameCountSpin->setMinimum(1);
    ui->sendFrameCountSpin->setMaximum(999999);
    ui->sendFrameCountSpin->setValue(1);
    ui->sendFrameCountSpin->setFixedWidth(qRound(70 * scale));

    ui->sendBtn->setFixedWidth(qRound(80 * scale));
    const QString blueBtn = btnStyle +
        "QPushButton { background-color: #2196F3; color: white; }"
        "QPushButton:hover { background-color: #0b7dda; }";
    ui->sendBtn->setStyleSheet(blueBtn);
    connect(ui->sendBtn, &QPushButton::clicked, this, &CanSessionWidget::onSendClicked);

    // 分割器比例
    ui->splitter->setStretchFactor(0, 3);
    ui->splitter->setStretchFactor(1, 1);
}

// ═══════════════════════════════════════════════════════════════
// 连接 / 断开
// ═══════════════════════════════════════════════════════════════

void CanSessionWidget::connectDevice(int channel, CanBaudRate baud, int adapterType)
{
    if (m_can->isOpen())
        disconnectDevice();

    CanInterface *newCan = nullptr;

    switch (static_cast<CanAdapterType>(adapterType)) {
#ifndef Q_OS_LINUX
    case CanAdapterType::PCAN:
        if (!m_pcan) { m_pcan = new PcanAdapter(this); linkSignals(m_pcan); }
        newCan = m_pcan;
        break;
    case CanAdapterType::GsUsb:
        if (!m_gsusb) { m_gsusb = new GsUsbAdapter(this); linkSignals(m_gsusb); }
        newCan = m_gsusb;
        break;
#endif
#ifdef Q_OS_LINUX
    case CanAdapterType::SocketCAN:
        if (!m_socketcan) { m_socketcan = new SocketCanAdapter(this); linkSignals(m_socketcan); }
        newCan = m_socketcan;
        break;
#endif
    default:
#ifdef Q_OS_LINUX
        if (!m_socketcan) { m_socketcan = new SocketCanAdapter(this); linkSignals(m_socketcan); }
        newCan = m_socketcan;
#else
        if (!m_pcan) { m_pcan = new PcanAdapter(this); linkSignals(m_pcan); }
        newCan = m_pcan;
#endif
        break;
    }

    m_can = newCan;
    m_adapterType = adapterType;

#ifdef Q_OS_LINUX
    if (adapterType == static_cast<int>(CanAdapterType::SocketCAN)) {
        ui->deviceLabel->setText("SocketCAN (请用 ip link 命令设置波特率)");
    } else {
        ui->deviceLabel->setText(newCan->adapterName());
    }
#else
    ui->deviceLabel->setText(newCan->adapterName());
#endif

    m_currentChannel = channel;

    // 计算逻辑通道号（用于显示）：PCAN channel 是 16 位 handle，取低 4 位
    if (adapterType == static_cast<int>(CanAdapterType::PCAN))
        m_channelIndex = channel & 0x0F;
    else
        m_channelIndex = channel;

    if (m_can->open(channel, baud)) {
        updateUiState(true);
        updateChannelCheckboxes();
        m_statusTimer->start(500);
    }
}

void CanSessionWidget::disconnectDevice()
{
    m_statusTimer->stop();
    stopSending();  // 停止逐帧发送
    m_can->close();
    updateUiState(false);
}

bool CanSessionWidget::isConnected() const
{
    return m_can && m_can->isOpen();
}

void CanSessionWidget::refreshDevices()
{
    updateChannelCheckboxes();
}

// ═══════════════════════════════════════════════════════════════
// 状态监控
// ═══════════════════════════════════════════════════════════════

void CanSessionWidget::onStatusCheck()
{
    if (!m_can || !m_can->isOpen()) return;

    if (!m_can->isAlive()) {
        m_statusTimer->stop();
        stopSending();
        m_can->close();
        updateUiState(false);

        ui->statusLabel->setText("⚠ 设备已断开");
        ui->statusLabel->setStyleSheet("color:red; font-weight:bold;");
        emit deviceDisconnected(m_sessionId);
    }
}

// ═══════════════════════════════════════════════════════════════
// UI 状态切换 & 通道复选框
// ═══════════════════════════════════════════════════════════════

void CanSessionWidget::updateUiState(bool connected)
{
    const QString greenBtn =
        "QPushButton { font-weight: bold; border-radius: 3px; padding: 4px 10px; "
        "background-color: #4CAF50; color: white; }"
        "QPushButton:hover { background-color: #45a049; }";
    const QString redBtn =
        "QPushButton { font-weight: bold; border-radius: 3px; padding: 4px 10px; "
        "background-color: #f44336; color: white; }"
        "QPushButton:hover { background-color: #d32f2f; }";

    if (connected) {
        ui->statusLabel->setText("● 已连接");
        ui->statusLabel->setToolTip("");
        ui->statusLabel->setStyleSheet("color:green; font-weight:bold;");
        ui->connectBtn->setText("断开");
        ui->connectBtn->setStyleSheet(redBtn);
        ui->baudCombo->setEnabled(false);
    } else {
        ui->statusLabel->setText("未连接");
        ui->statusLabel->setToolTip("");
        ui->statusLabel->setStyleSheet("color:gray; font-weight:bold;");
        ui->connectBtn->setText("连接");
        ui->connectBtn->setStyleSheet(greenBtn);
        ui->baudCombo->setEnabled(true);

        qDeleteAll(m_channelChks);
        m_channelChks.clear();
    }
}

void CanSessionWidget::updateChannelCheckboxes()
{
    qDeleteAll(m_channelChks);
    m_channelChks.clear();

    if (!m_can) return;

    QList<CanDeviceInfo> devices = m_can->scanDevices();

    if (devices.isEmpty() && m_currentChannel > 0) {
        auto *chk = new QCheckBox(QString("CH%1").arg(m_currentChannel & 0x0F));
        chk->setChecked(true);
        chk->setToolTip(QString("通道 0x%1").arg(m_currentChannel, 2, 16, QChar('0')));
        m_channelChks.append(chk);
        ui->channelChkLayout->addWidget(chk);
        return;
    }

    for (const auto &dev : devices) {
        auto *chk = new QCheckBox(QString("CH%1").arg(dev.channel & 0x0F));
        chk->setChecked(true);
        chk->setToolTip(dev.name);
        m_channelChks.append(chk);
        ui->channelChkLayout->addWidget(chk);
    }
}

// ═══════════════════════════════════════════════════════════════
// 槽
// ═══════════════════════════════════════════════════════════════

void CanSessionWidget::onConnectClicked()
{
    if (m_can->isOpen()) {
        disconnectDevice();
        return;
    }

    if (m_currentChannel >= 0) {
        CanBaudRate baud = baudRateFromString(ui->baudCombo->currentText());

        connectDevice(m_currentChannel, baud, m_adapterType);
    }
}

void CanSessionWidget::onSendClicked()
{
    if (!m_can->isOpen()) return;

    // ── 如果正在发送 → 停止 ──
    if (m_sending) {
        stopSending();
        return;
    }

    prepareAndStartSend();
}

void CanSessionWidget::prepareAndStartSend()
{
    if (!m_can->isOpen()) return;

    // 解析 16 进制数据（仅允许 0-9, A-F, a-f, 空格）
    QString dataStr = ui->sendDataEdit->toPlainText().trimmed();
    QString filtered;
    for (const QChar &ch : dataStr) {
        if (ch.isSpace() || (ch >= '0' && ch <= '9') ||
            (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
            filtered += ch;
        }
    }
    if (filtered != dataStr) {
        ui->sendDataEdit->setPlainText(filtered);
        dataStr = filtered;
    }

    QStringList bytes = dataStr.split(' ', Qt::SkipEmptyParts);

    m_pendingMsg = CanMessage();
    m_pendingMsg.direction = CanDirection::Tx;
    m_pendingMsg.channel = m_channelIndex;

    QString idText = ui->sendIdEdit->text().trimmed();
    bool ok = false;
    if (idText.startsWith("0x", Qt::CaseInsensitive))
        m_pendingMsg.id = idText.mid(2).toUInt(&ok, 16);
    else
        m_pendingMsg.id = idText.toUInt(&ok, 16);
    if (!ok) { m_pendingMsg.id = 0x123; }

    int typeIdx = ui->sendTypeCombo->currentIndex();
    if (typeIdx == 0) m_pendingMsg.type = CanFrameType::StandardData;
    else if (typeIdx == 1) m_pendingMsg.type = CanFrameType::ExtendedData;
    else m_pendingMsg.type = CanFrameType::Remote;

    int dlcVal = qMin(bytes.size(), 64);
    m_pendingMsg.dlc = static_cast<uint8_t>(qMax(dlcVal, ui->sendDlcSpin->value()));
    if (dlcVal > 0) {
        m_pendingMsg.dlc = static_cast<uint8_t>(dlcVal);
    }

    int dataLen = qMin(bytes.size(), 64);
    for (int i = 0; i < dataLen; ++i) {
        m_pendingMsg.data[i] = static_cast<uint8_t>(bytes[i].toUInt(&ok, 16));
        if (!ok) m_pendingMsg.data[i] = 0;
    }

    // 设置发送状态
    m_frameRemaining = ui->sendFrameCountSpin->value();
    m_sending = true;

    // 帧间隔：0 用 1ms 快速发送，>0 用指定间隔
    int intervalMs = ui->sendPeriodSpin->value();
    if (intervalMs <= 0) intervalMs = 1;

    // 帧数 > 1 或帧间隔 > 1 时显示"停止"按钮（仅单帧最快发送不切换）
    bool multiFrame = (m_frameRemaining > 1 || intervalMs > 1);
    if (multiFrame) {
        updateSendButtonState(true);
    }

    m_frameTimer->start(intervalMs);
}

void CanSessionWidget::onSendOneFrame()
{
    if (!m_sending || m_frameRemaining <= 0 || !m_can->isOpen()) {
        stopSending();
        return;
    }

    m_pendingMsg.timestamp = QDateTime::currentDateTime();

    if (m_can->sendMessage(m_pendingMsg)) {
        m_txCount++;
        addMessageToTable(m_pendingMsg);
    }

    m_frameRemaining--;

    if (m_frameRemaining <= 0) {
        stopSending();
    }
}

void CanSessionWidget::stopSending()
{
    m_frameTimer->stop();
    bool hadMultiFrame = (m_frameRemaining > 1 || ui->sendPeriodSpin->value() > 0);
    m_sending = false;
    m_frameRemaining = 0;
    // 仅当之前更新过 UI 时才恢复
    if (hadMultiFrame) {
        updateSendButtonState(false);
    }
}

void CanSessionWidget::updateSendButtonState(bool sending)
{
    const qreal scale = QApplication::primaryScreen()->devicePixelRatio();
    const QString btnStyle = QStringLiteral(
        "QPushButton { font-weight: bold; border-radius: 3px; padding: 4px 10px; }");

    if (sending) {
        const QString redBtn = btnStyle +
            "QPushButton { background-color: #f44336; color: white; }"
            "QPushButton:hover { background-color: #d32f2f; }";
        ui->sendBtn->setText("停止");
        ui->sendBtn->setStyleSheet(redBtn);
        ui->sendBtn->setFixedWidth(qRound(80 * scale));
        // 发送期间禁用参数编辑
        ui->sendIdEdit->setEnabled(false);
        ui->sendTypeCombo->setEnabled(false);
        ui->sendDlcSpin->setEnabled(false);
        ui->sendDataEdit->setEnabled(false);
        ui->sendFrameCountSpin->setEnabled(false);
        ui->sendPeriodSpin->setEnabled(false);
    } else {
        const QString blueBtn = btnStyle +
            "QPushButton { background-color: #2196F3; color: white; }"
            "QPushButton:hover { background-color: #0b7dda; }";
        ui->sendBtn->setText("发送");
        ui->sendBtn->setStyleSheet(blueBtn);
        ui->sendBtn->setFixedWidth(qRound(80 * scale));
        // 恢复参数编辑
        ui->sendIdEdit->setEnabled(true);
        ui->sendTypeCombo->setEnabled(true);
        ui->sendDlcSpin->setEnabled(true);
        ui->sendDataEdit->setEnabled(true);
        ui->sendFrameCountSpin->setEnabled(true);
        ui->sendPeriodSpin->setEnabled(true);
    }
}

void CanSessionWidget::onClearClicked()
{
    ui->rxTable->setRowCount(0);
    m_rxCount = 0;
    m_txCount = 0;
    updateStats();
}

void CanSessionWidget::onSaveClicked()
{
    if (ui->rxTable->rowCount() == 0) return;

    QString defaultName = QString("can_log_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QFileDialog::getSaveFileName(
        this, "保存 CAN 报文", defaultName,
        "CSV 文件 (*.csv);;所有文件 (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QChar(0xFEFF);

    out << "时间,ID,通道,类型,DLC,数据,方向\n";

    for (int row = 0; row < ui->rxTable->rowCount(); ++row) {
        for (int col = 0; col < ui->rxTable->columnCount(); ++col) {
            if (col > 0) out << ",";
            auto *item = ui->rxTable->item(row, col);
            if (item) {
                QString text = item->text();
                if (col == ColData && !text.isEmpty())
                    out << "\"" << text << "\"";
                else
                    out << text;
            }
        }
        out << "\n";
    }

    file.close();
}

// ─── 接收消息 ─────────────────────────────────────────────────

void CanSessionWidget::onMessageReceived(const CanMessage &msg)
{
    m_rxCount++;
    addMessageToTable(msg);
}

void CanSessionWidget::addMessageToTable(const CanMessage &msg)
{
    int row = ui->rxTable->rowCount();

    if (row >= m_maxTableRows) {
        ui->rxTable->removeRow(0);
        row--;
    }

    ui->rxTable->insertRow(row);

    auto *timeItem = new QTableWidgetItem(msg.timestamp.toString("hh:mm:ss.zzz"));
    timeItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColTime, timeItem);

    auto *idItem = new QTableWidgetItem(msg.idString());
    idItem->setTextAlignment(Qt::AlignCenter);
    if (msg.type == CanFrameType::ExtendedData)
        idItem->setForeground(QColor("#E91E63"));
    ui->rxTable->setItem(row, ColId, idItem);

    auto *chItem = new QTableWidgetItem(QString("CH%1").arg(msg.channel));
    chItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColCh, chItem);

    auto *typeItem = new QTableWidgetItem(msg.typeString());
    typeItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColType, typeItem);

    auto *dlcItem = new QTableWidgetItem(QString::number(msg.dlc));
    dlcItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColDlc, dlcItem);

    auto *dataItem = new QTableWidgetItem(msg.dataHex());
    dataItem->setFont(QFont("Consolas", 9));
    ui->rxTable->setItem(row, ColData, dataItem);

    auto *dirItem = new QTableWidgetItem(msg.direction == CanDirection::Rx ? "Rx" : "Tx");
    dirItem->setTextAlignment(Qt::AlignCenter);
    dirItem->setForeground(msg.direction == CanDirection::Rx ? QColor("#2196F3") : QColor("#4CAF50"));
    ui->rxTable->setItem(row, ColDir, dirItem);

    if (ui->autoScrollChk->isChecked())
        ui->rxTable->scrollToBottom();

    updateStats();
}

void CanSessionWidget::updateStats()
{
    ui->rxCountLabel->setText(QString("Rx: %1  |  Tx: %2").arg(m_rxCount).arg(m_txCount));
}
