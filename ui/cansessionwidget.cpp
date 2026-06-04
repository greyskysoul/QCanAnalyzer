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
    connect(m_pcan, &CanInterface::messageReceived,
            this, &CanSessionWidget::onMessageReceived);
    connect(m_pcan, &CanInterface::errorOccurred, this, [this](const QString &err) {
        QString shortErr = err;
        if (shortErr.length() > 50)
            shortErr = shortErr.left(47) + "...";
        ui->statusLabel->setText("⚠ " + shortErr);
        ui->statusLabel->setToolTip(err);
        ui->statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });
#else
    m_socketcan = new SocketCanAdapter(this);
    m_can = m_socketcan;
    connect(m_socketcan, &CanInterface::messageReceived,
            this, &CanSessionWidget::onMessageReceived);
    connect(m_socketcan, &CanInterface::errorOccurred, this, [this](const QString &err) {
        QString shortErr = err;
        if (shortErr.length() > 50)
            shortErr = shortErr.left(47) + "...";
        ui->statusLabel->setText("⚠ " + shortErr);
        ui->statusLabel->setToolTip(err);
        ui->statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });
#endif

    m_periodicTimer = new QTimer(this);
    connect(m_periodicTimer, &QTimer::timeout, this, &CanSessionWidget::onSendClicked);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &CanSessionWidget::onStatusCheck);

    setWindowTitle(QString("CAN Session %1").arg(sessionId));
}

CanSessionWidget::~CanSessionWidget()
{
    disconnectDevice();
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
    disconnectDevice();
}

void CanSessionWidget::setupUi()
{
    qreal scale = QApplication::primaryScreen()->devicePixelRatio();

    const QString btnStyle = QString(
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
    ui->rxTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(0, qRound(130 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(1, qRound(90 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(2, qRound(55 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(3, qRound(45 * scale));
    ui->rxTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(5, qRound(50 * scale));
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
    ui->sendDlcSpin->setRange(0, 8);
    ui->sendDlcSpin->setValue(8);
    ui->sendDlcSpin->setFixedWidth(qRound(55 * scale));
    ui->sendDataEdit->setMinimumWidth(qRound(180 * scale));

    connect(ui->periodicSendChk, &QCheckBox::toggled, this, [this](bool checked) {
        ui->sendPeriodSpin->setEnabled(checked);
        if (checked && m_can->isOpen())
            m_periodicTimer->start(ui->sendPeriodSpin->value());
        else
            m_periodicTimer->stop();
    });
    ui->sendPeriodSpin->setRange(10, 10000);
    ui->sendPeriodSpin->setValue(1000);
    connect(ui->sendPeriodSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_periodicTimer->isActive())
            m_periodicTimer->setInterval(val);
    });

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

    if (m_can->open(channel, baud)) {
        updateUiState(true);
        updateChannelCheckboxes();
        m_statusTimer->start(500);

        if (ui->periodicSendChk->isChecked())
            m_periodicTimer->start(ui->sendPeriodSpin->value());
    }
}

void CanSessionWidget::disconnectDevice()
{
    m_statusTimer->stop();
    m_periodicTimer->stop();
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
        m_periodicTimer->stop();
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

    if (m_currentChannel > 0) {
        QString baudStr = ui->baudCombo->currentText();
        CanBaudRate baud = CanBaudRate::BR_500K;
        if (baudStr == "1M")    baud = CanBaudRate::BR_1M;
        else if (baudStr == "800K")  baud = CanBaudRate::BR_800K;
        else if (baudStr == "250K")  baud = CanBaudRate::BR_250K;
        else if (baudStr == "125K")  baud = CanBaudRate::BR_125K;
        else if (baudStr == "100K")  baud = CanBaudRate::BR_100K;
        else if (baudStr == "50K")   baud = CanBaudRate::BR_50K;
        else if (baudStr == "20K")   baud = CanBaudRate::BR_20K;
        else if (baudStr == "10K")   baud = CanBaudRate::BR_10K;
        else if (baudStr == "5K")    baud = CanBaudRate::BR_5K;

        connectDevice(m_currentChannel, baud);
    }
}

void CanSessionWidget::onSendClicked()
{
    if (!m_can->isOpen()) return;

    CanMessage msg;
    msg.direction = CanDirection::Tx;
    msg.timestamp = QDateTime::currentDateTime();

    QString idText = ui->sendIdEdit->text().trimmed();
    bool ok = false;
    if (idText.startsWith("0x", Qt::CaseInsensitive))
        msg.id = idText.mid(2).toUInt(&ok, 16);
    else
        msg.id = idText.toUInt(&ok, 16);
    if (!ok) { msg.id = 0x123; }

    int typeIdx = ui->sendTypeCombo->currentIndex();
    if (typeIdx == 0) msg.type = CanFrameType::StandardData;
    else if (typeIdx == 1) msg.type = CanFrameType::ExtendedData;
    else msg.type = CanFrameType::Remote;

    msg.dlc = static_cast<uint8_t>(ui->sendDlcSpin->value());

    QString dataStr = ui->sendDataEdit->text().trimmed();
    QStringList bytes = dataStr.split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < bytes.size() && i < 8; ++i) {
        msg.data[i] = static_cast<uint8_t>(bytes[i].toUInt(&ok, 16));
        if (!ok) msg.data[i] = 0;
    }

    if (m_can->sendMessage(msg)) {
        m_txCount++;
        addMessageToTable(msg);
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

    out << "时间,ID,类型,DLC,数据,方向\n";

    for (int row = 0; row < ui->rxTable->rowCount(); ++row) {
        for (int col = 0; col < ui->rxTable->columnCount(); ++col) {
            if (col > 0) out << ",";
            auto *item = ui->rxTable->item(row, col);
            if (item) {
                QString text = item->text();
                if (col == 4 && !text.isEmpty())
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
    ui->rxTable->setItem(row, 0, timeItem);

    auto *idItem = new QTableWidgetItem(msg.idString());
    idItem->setTextAlignment(Qt::AlignCenter);
    if (msg.type == CanFrameType::ExtendedData)
        idItem->setForeground(QColor("#E91E63"));
    ui->rxTable->setItem(row, 1, idItem);

    auto *typeItem = new QTableWidgetItem(msg.typeString());
    typeItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, 2, typeItem);

    auto *dlcItem = new QTableWidgetItem(QString::number(msg.dlc));
    dlcItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, 3, dlcItem);

    auto *dataItem = new QTableWidgetItem(msg.dataHex());
    dataItem->setFont(QFont("Consolas", 9));
    ui->rxTable->setItem(row, 4, dataItem);

    auto *dirItem = new QTableWidgetItem(msg.direction == CanDirection::Rx ? "Rx" : "Tx");
    dirItem->setTextAlignment(Qt::AlignCenter);
    dirItem->setForeground(msg.direction == CanDirection::Rx ? QColor("#2196F3") : QColor("#4CAF50"));
    ui->rxTable->setItem(row, 5, dirItem);

    if (ui->autoScrollChk->isChecked())
        ui->rxTable->scrollToBottom();

    updateStats();
}

void CanSessionWidget::updateStats()
{
    ui->rxCountLabel->setText(QString("Rx: %1  |  Tx: %2").arg(m_rxCount).arg(m_txCount));
}
