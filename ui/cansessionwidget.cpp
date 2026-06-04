#include "cansessionwidget.h"
#ifndef Q_OS_LINUX
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#else
#include "can/socketcanadapter.h"
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QScrollBar>
#include <QApplication>
#include <QScreen>
#include <QFileDialog>
#include <QTextStream>
#include <QDateTime>

CanSessionWidget::CanSessionWidget(int sessionId, QWidget *parent)
    : QWidget(parent)
    , m_sessionId(sessionId)
{
    setupUi();

#ifndef Q_OS_LINUX
    // 默认创建 PCAN（可按需切换）
    m_pcan = new PcanAdapter(this);
    m_can = m_pcan;

    connect(m_pcan, &CanInterface::messageReceived,
            this, &CanSessionWidget::onMessageReceived);
    connect(m_pcan, &CanInterface::errorOccurred, this, [this](const QString &err) {
        // Label 显示截断文本，完整文本放入 tooltip
        QString shortErr = err;
        if (shortErr.length() > 50)
            shortErr = shortErr.left(47) + "...";
        m_statusLabel->setText("⚠ " + shortErr);
        m_statusLabel->setToolTip(err);  // 鼠标悬停看完整错误
        m_statusLabel->setStyleSheet("color:orange; font-weight:bold;");
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
        m_statusLabel->setText("⚠ " + shortErr);
        m_statusLabel->setToolTip(err);
        m_statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });
#endif

    m_periodicTimer = new QTimer(this);
    connect(m_periodicTimer, &QTimer::timeout, this, &CanSessionWidget::onSendClicked);

    // 设备状态监控 (每500ms检查一次)
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &CanSessionWidget::onStatusCheck);

    setWindowTitle(QString("CAN Session %1").arg(sessionId));
}

CanSessionWidget::~CanSessionWidget()
{
    disconnectDevice();
}

void CanSessionWidget::linkSignals(CanInterface *iface)
{
    connect(iface, &CanInterface::messageReceived,
            this, &CanSessionWidget::onMessageReceived);
    connect(iface, &CanInterface::errorOccurred, this, [this](const QString &err) {
        QString shortErr = err;
        if (shortErr.length() > 50)
            shortErr = shortErr.left(47) + "...";
        m_statusLabel->setText("⚠ " + shortErr);
        m_statusLabel->setToolTip(err);
        m_statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });
    disconnectDevice();
}

void CanSessionWidget::setupUi()
{
    qreal scale = QApplication::primaryScreen()->devicePixelRatio();

    // ─── 统一样式表 ───
    const QString btnStyle = QString(
        "QPushButton { font-weight: bold; border-radius: 3px; padding: 4px 10px; }");
    const QString greenBtn = btnStyle +
        "QPushButton { background-color: #4CAF50; color: white; }"
        "QPushButton:hover { background-color: #45a049; }";
    const QString redBtn = btnStyle +
        "QPushButton { background-color: #f44336; color: white; }"
        "QPushButton:hover { background-color: #d32f2f; }";
    const QString blueBtn = btnStyle +
        "QPushButton { background-color: #2196F3; color: white; }"
        "QPushButton:hover { background-color: #0b7dda; }";
    const QString grayBtn = btnStyle +
        "QPushButton { background-color: #607d8b; color: white; }"
        "QPushButton:hover { background-color: #455a64; }";

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ─── 连接控制栏 ───
    auto *connLayout = new QHBoxLayout();
    connLayout->setSpacing(6);

    connLayout->addWidget(new QLabel("设备:"));
    m_deviceLabel = new QLabel("—");
    m_deviceLabel->setStyleSheet("font-weight:bold; color:#2c3e50;");
    m_deviceLabel->setMinimumWidth(qRound(120 * scale));

    connLayout->addWidget(m_deviceLabel);
    connLayout->addWidget(new QLabel("波特率:"));
    m_baudCombo = new QComboBox();
    m_baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    m_baudCombo->setCurrentText("500K");

    m_connectBtn = new QPushButton("连接");
    m_connectBtn->setFixedWidth(qRound(70 * scale));
    m_connectBtn->setStyleSheet(greenBtn);
    connect(m_connectBtn, &QPushButton::clicked, this, &CanSessionWidget::onConnectClicked);

    m_statusLabel = new QLabel("未连接");
    m_statusLabel->setStyleSheet("color:gray; font-weight:bold;");
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setMinimumWidth(qRound(120 * scale));

    connLayout->addWidget(m_baudCombo);
    connLayout->addWidget(m_connectBtn);
    connLayout->addWidget(m_statusLabel, 1);
    connLayout->addStretch();

    mainLayout->addLayout(connLayout);

    // ─── 通道接收复选框 ───
    auto *chRow = new QHBoxLayout();
    chRow->setSpacing(8);
    chRow->addWidget(new QLabel("接收通道:"));
    m_channelChkLayout = new QHBoxLayout();
    m_channelChkLayout->setSpacing(6);
    chRow->addLayout(m_channelChkLayout);
    chRow->addStretch();
    mainLayout->addLayout(chRow);

    // ─── 分割器 (接收表 / 发送区) ───
    auto *splitter = new QSplitter(Qt::Vertical, this);

    // ── 接收表格 ──
    auto *rxGroup = new QGroupBox("接收报文");
    auto *rxLayout = new QVBoxLayout(rxGroup);

    m_rxTable = new QTableWidget(0, 6, this);
    m_rxTable->setHorizontalHeaderLabels({"时间", "ID", "类型", "DLC", "数据", "方向"});
    m_rxTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_rxTable->horizontalHeader()->resizeSection(0, qRound(130 * scale));
    m_rxTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_rxTable->horizontalHeader()->resizeSection(1, qRound(90 * scale));
    m_rxTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_rxTable->horizontalHeader()->resizeSection(2, qRound(55 * scale));
    m_rxTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_rxTable->horizontalHeader()->resizeSection(3, qRound(45 * scale));
    m_rxTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_rxTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_rxTable->horizontalHeader()->resizeSection(5, qRound(50 * scale));
    m_rxTable->horizontalHeader()->setStretchLastSection(false);
    m_rxTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rxTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rxTable->setAlternatingRowColors(true);
    m_rxTable->verticalHeader()->setVisible(false);
    m_rxTable->verticalHeader()->setDefaultSectionSize(qRound(24 * scale));

    auto *rxStatusLayout = new QHBoxLayout();
    m_rxCountLabel = new QLabel("Rx: 0");
    m_autoScrollChk = new QCheckBox("自动滚动");
    m_autoScrollChk->setChecked(true);

    m_saveBtn = new QPushButton("保存");
    m_saveBtn->setFixedWidth(qRound(55 * scale));
    m_saveBtn->setStyleSheet(grayBtn);
    connect(m_saveBtn, &QPushButton::clicked, this, &CanSessionWidget::onSaveClicked);

    m_clearBtn = new QPushButton("清除");
    m_clearBtn->setFixedWidth(qRound(55 * scale));
    m_clearBtn->setStyleSheet(grayBtn);
    connect(m_clearBtn, &QPushButton::clicked, this, &CanSessionWidget::onClearClicked);

    rxStatusLayout->addWidget(m_rxCountLabel);
    rxStatusLayout->addStretch();
    rxStatusLayout->addWidget(m_autoScrollChk);
    rxStatusLayout->addWidget(m_saveBtn);
    rxStatusLayout->addWidget(m_clearBtn);

    rxLayout->addWidget(m_rxTable);
    rxLayout->addLayout(rxStatusLayout);
    splitter->addWidget(rxGroup);

    // ── 发送面板 ──
    auto *txGroup = new QGroupBox("发送报文");
    auto *txLayout = new QVBoxLayout(txGroup);

    auto *txFormLayout = new QHBoxLayout();
    txFormLayout->setSpacing(6);

    txFormLayout->addWidget(new QLabel("ID:"));
    m_sendIdEdit = new QLineEdit("0x123");
    m_sendIdEdit->setMaximumWidth(qRound(100 * scale));
    txFormLayout->addWidget(m_sendIdEdit);

    txFormLayout->addWidget(new QLabel("类型:"));
    m_sendTypeCombo = new QComboBox();
    m_sendTypeCombo->addItems({"标准数据帧", "扩展数据帧", "远程帧"});
    txFormLayout->addWidget(m_sendTypeCombo);

    txFormLayout->addWidget(new QLabel("DLC:"));
    m_sendDlcSpin = new QSpinBox();
    m_sendDlcSpin->setRange(0, 8);
    m_sendDlcSpin->setValue(8);
    m_sendDlcSpin->setFixedWidth(qRound(55 * scale));
    txFormLayout->addWidget(m_sendDlcSpin);

    txFormLayout->addWidget(new QLabel("数据(Hex):"));
    m_sendDataEdit = new QLineEdit("00 00 00 00 00 00 00 00");
    m_sendDataEdit->setMinimumWidth(qRound(180 * scale));
    txFormLayout->addWidget(m_sendDataEdit);
    txFormLayout->addStretch();

    txLayout->addLayout(txFormLayout);

    auto *txBtnLayout = new QHBoxLayout();
    m_periodicSendChk = new QCheckBox("周期发送");
    m_sendPeriodSpin = new QSpinBox();
    m_sendPeriodSpin->setRange(10, 10000);
    m_sendPeriodSpin->setValue(1000);
    m_sendPeriodSpin->setSuffix(" ms");
    m_sendPeriodSpin->setEnabled(false);

    connect(m_periodicSendChk, &QCheckBox::toggled, this, [this](bool checked) {
        m_sendPeriodSpin->setEnabled(checked);
        if (checked && m_can->isOpen())
            m_periodicTimer->start(m_sendPeriodSpin->value());
        else
            m_periodicTimer->stop();
    });
    connect(m_sendPeriodSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_periodicTimer->isActive())
            m_periodicTimer->setInterval(val);
    });

    m_sendBtn = new QPushButton("发送");
    m_sendBtn->setFixedWidth(qRound(80 * scale));
    m_sendBtn->setStyleSheet(blueBtn);
    connect(m_sendBtn, &QPushButton::clicked, this, &CanSessionWidget::onSendClicked);

    txBtnLayout->addWidget(m_periodicSendChk);
    txBtnLayout->addWidget(m_sendPeriodSpin);
    txBtnLayout->addStretch();
    txBtnLayout->addWidget(m_sendBtn);
    txLayout->addLayout(txBtnLayout);

    splitter->addWidget(txGroup);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);
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
        m_deviceLabel->setText("SocketCAN (请用 ip link 命令设置波特率)");
    } else {
        m_deviceLabel->setText(newCan->adapterName());
    }
#else
    m_deviceLabel->setText(newCan->adapterName());
#endif

    m_currentChannel = channel;

    if (m_can->open(channel, baud)) {
        updateUiState(true);
        updateChannelCheckboxes();
        m_statusTimer->start(500);

        if (m_periodicSendChk->isChecked())
            m_periodicTimer->start(m_sendPeriodSpin->value());
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
    // 不再需要填充下拉列表，仅更新通道复选框
    updateChannelCheckboxes();
}

// ═══════════════════════════════════════════════════════════════
// 状态监控
// ═══════════════════════════════════════════════════════════════

void CanSessionWidget::onStatusCheck()
{
    if (!m_can || !m_can->isOpen()) return;

    // 用 isAlive() 检查连接，不重新扫描设备
    if (!m_can->isAlive()) {
        m_statusTimer->stop();
        m_periodicTimer->stop();
        m_can->close();
        updateUiState(false);

        m_statusLabel->setText("⚠ 设备已断开");
        m_statusLabel->setStyleSheet("color:red; font-weight:bold;");
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
        m_statusLabel->setText("● 已连接");
        m_statusLabel->setToolTip("");
        m_statusLabel->setStyleSheet("color:green; font-weight:bold;");
        m_connectBtn->setText("断开");
        m_connectBtn->setStyleSheet(redBtn);
        m_baudCombo->setEnabled(false);
    } else {
        m_statusLabel->setText("未连接");
        m_statusLabel->setToolTip("");
        m_statusLabel->setStyleSheet("color:gray; font-weight:bold;");
        m_connectBtn->setText("连接");
        m_connectBtn->setStyleSheet(greenBtn);
        m_baudCombo->setEnabled(true);

        // 清除通道复选框
        qDeleteAll(m_channelChks);
        m_channelChks.clear();
    }
}

void CanSessionWidget::updateChannelCheckboxes()
{
    qDeleteAll(m_channelChks);
    m_channelChks.clear();

    if (!m_can) return;

    // 通过基类指针扫描设备获取通道列表
    QList<CanDeviceInfo> devices = m_can->scanDevices();

    // 如果没扫到设备但有当前通道号，至少显示当前通道
    if (devices.isEmpty() && m_currentChannel > 0) {
        auto *chk = new QCheckBox(QString("CH%1").arg(m_currentChannel & 0x0F));
        chk->setChecked(true);
        chk->setToolTip(QString("通道 0x%1").arg(m_currentChannel, 2, 16, QChar('0')));
        m_channelChks.append(chk);
        m_channelChkLayout->addWidget(chk);
        return;
    }

    for (const auto &dev : devices) {
        auto *chk = new QCheckBox(QString("CH%1").arg(dev.channel & 0x0F));
        chk->setChecked(true);
        chk->setToolTip(dev.name);
        m_channelChks.append(chk);
        m_channelChkLayout->addWidget(chk);
    }
}

// ═══════════════════════════════════════════════════════════════
// 槽
// ═══════════════════════════════════════════════════════════════

void CanSessionWidget::onConnectClicked()
{
    // 会话已经通过配置对话框连接，这里只负责断开/重连
    if (m_can->isOpen()) {
        disconnectDevice();
        return;
    }

    // 重新连接（使用上次参数）
    if (m_currentChannel > 0) {
        QString baudStr = m_baudCombo->currentText();
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

    // 解析 ID
    QString idText = m_sendIdEdit->text().trimmed();
    bool ok = false;
    if (idText.startsWith("0x", Qt::CaseInsensitive))
        msg.id = idText.mid(2).toUInt(&ok, 16);
    else
        msg.id = idText.toUInt(&ok, 16);
    if (!ok) { msg.id = 0x123; }

    // 类型
    int typeIdx = m_sendTypeCombo->currentIndex();
    if (typeIdx == 0) msg.type = CanFrameType::StandardData;
    else if (typeIdx == 1) msg.type = CanFrameType::ExtendedData;
    else msg.type = CanFrameType::Remote;

    // DLC
    msg.dlc = static_cast<uint8_t>(m_sendDlcSpin->value());

    // 解析数据
    QString dataStr = m_sendDataEdit->text().trimmed();
    QStringList bytes = dataStr.split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < bytes.size() && i < 8; ++i) {
        msg.data[i] = static_cast<uint8_t>(bytes[i].toUInt(&ok, 16));
        if (!ok) msg.data[i] = 0;
    }

    if (m_can->sendMessage(msg)) {
        m_txCount++;
        // 把发送的消息也显示在表格里
        addMessageToTable(msg);
    }
}

void CanSessionWidget::onClearClicked()
{
    m_rxTable->setRowCount(0);
    m_rxCount = 0;
    m_txCount = 0;
    updateStats();
}

void CanSessionWidget::onSaveClicked()
{
    if (m_rxTable->rowCount() == 0) return;

    QString defaultName = QString("can_log_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QFileDialog::getSaveFileName(
        this, "保存 CAN 报文", defaultName,
        "CSV 文件 (*.csv);;所有文件 (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    // BOM for Excel UTF-8
    out.setCodec("UTF-8");
    out << QChar(0xFEFF);

    // 表头
    out << "时间,ID,类型,DLC,数据,方向\n";

    // 数据行
    for (int row = 0; row < m_rxTable->rowCount(); ++row) {
        for (int col = 0; col < m_rxTable->columnCount(); ++col) {
            if (col > 0) out << ",";
            auto *item = m_rxTable->item(row, col);
            if (item) {
                QString text = item->text();
                // 数据列包含空格，需要引号包裹
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
    int row = m_rxTable->rowCount();

    // 限制最大行数
    if (row >= m_maxTableRows) {
        m_rxTable->removeRow(0);
        row--;
    }

    m_rxTable->insertRow(row);

    // 时间
    auto *timeItem = new QTableWidgetItem(msg.timestamp.toString("hh:mm:ss.zzz"));
    timeItem->setTextAlignment(Qt::AlignCenter);
    m_rxTable->setItem(row, 0, timeItem);

    // ID
    auto *idItem = new QTableWidgetItem(msg.idString());
    idItem->setTextAlignment(Qt::AlignCenter);
    if (msg.type == CanFrameType::ExtendedData)
        idItem->setForeground(QColor("#E91E63")); // 扩展帧粉色
    m_rxTable->setItem(row, 1, idItem);

    // 类型
    auto *typeItem = new QTableWidgetItem(msg.typeString());
    typeItem->setTextAlignment(Qt::AlignCenter);
    m_rxTable->setItem(row, 2, typeItem);

    // DLC
    auto *dlcItem = new QTableWidgetItem(QString::number(msg.dlc));
    dlcItem->setTextAlignment(Qt::AlignCenter);
    m_rxTable->setItem(row, 3, dlcItem);

    // 数据
    auto *dataItem = new QTableWidgetItem(msg.dataHex());
    dataItem->setFont(QFont("Consolas", 9));
    m_rxTable->setItem(row, 4, dataItem);

    // 方向
    auto *dirItem = new QTableWidgetItem(msg.direction == CanDirection::Rx ? "Rx" : "Tx");
    dirItem->setTextAlignment(Qt::AlignCenter);
    dirItem->setForeground(msg.direction == CanDirection::Rx ? QColor("#2196F3") : QColor("#4CAF50"));
    m_rxTable->setItem(row, 5, dirItem);

    // 自动滚动
    if (m_autoScrollChk->isChecked())
        m_rxTable->scrollToBottom();

    updateStats();
}

void CanSessionWidget::updateStats()
{
    m_rxCountLabel->setText(QString("Rx: %1  |  Tx: %2").arg(m_rxCount).arg(m_txCount));
}
