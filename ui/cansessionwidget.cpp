#include "cansessionwidget.h"
#include "can/pcanadapter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QScrollBar>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>

CanSessionWidget::CanSessionWidget(int sessionId, QWidget *parent)
    : QWidget(parent)
    , m_sessionId(sessionId)
{
    setupUi();

    // 创建 PCAN 适配器
    m_pcan = new PcanAdapter(this);
    m_can = m_pcan;

    connect(m_pcan, &CanInterface::messageReceived,
            this, &CanSessionWidget::onMessageReceived);
    connect(m_pcan, &CanInterface::errorOccurred, this, [this](const QString &err) {
        m_statusLabel->setText("⚠ " + err);
        m_statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });

    // 定时发送
    m_periodicTimer = new QTimer(this);
    connect(m_periodicTimer, &QTimer::timeout, this, &CanSessionWidget::onSendClicked);

    // 初始扫描
    refreshDevices();

    setWindowTitle(QString("CAN Session %1").arg(sessionId));
}

CanSessionWidget::~CanSessionWidget()
{
    disconnectDevice();
}

void CanSessionWidget::setupUi()
{
    // DPI 缩放系数
    qreal scale = QApplication::primaryScreen()->devicePixelRatio();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ─── 连接控制栏 ───
    auto *connLayout = new QHBoxLayout();
    connLayout->setSpacing(6);

    auto *devLabel = new QLabel("设备:");
    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(qRound(150 * scale));

    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setFixedWidth(qRound(50 * scale));
    connect(m_refreshBtn, &QPushButton::clicked, this, &CanSessionWidget::refreshDevices);

    auto *baudLabel = new QLabel("波特率:");
    m_baudCombo = new QComboBox();
    m_baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    m_baudCombo->setCurrentText("500K");

    m_connectBtn = new QPushButton("连接");
    m_connectBtn->setFixedWidth(qRound(70 * scale));
    m_connectBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #45a049; }");
    connect(m_connectBtn, &QPushButton::clicked, this, &CanSessionWidget::onConnectClicked);

    m_statusLabel = new QLabel("未连接");
    m_statusLabel->setStyleSheet("color:gray; font-weight:bold;");

    connLayout->addWidget(devLabel);
    connLayout->addWidget(m_deviceCombo);
    connLayout->addWidget(m_refreshBtn);
    connLayout->addWidget(baudLabel);
    connLayout->addWidget(m_baudCombo);
    connLayout->addWidget(m_connectBtn);
    connLayout->addWidget(m_statusLabel);
    connLayout->addStretch();

    mainLayout->addLayout(connLayout);

    // ─── 分割器 (接收表 / 发送区) ───
    auto *splitter = new QSplitter(Qt::Vertical, this);

    // ── 接收表格 ──
    auto *rxGroup = new QGroupBox("接收报文");
    auto *rxLayout = new QVBoxLayout(rxGroup);

    m_rxTable = new QTableWidget(0, 6, this);
    m_rxTable->setHorizontalHeaderLabels({"时间", "ID", "类型", "DLC", "数据", "方向"});
    // 列 0 时间:   固定 ~130px (DPI自适应)
    // 列 1 ID:     固定 ~90px
    // 列 2 类型:   固定 ~55px
    // 列 3 DLC:    固定 ~45px
    // 列 4 数据:   Stretch (自动填充剩余)
    // 列 5 方向:   固定 ~50px
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
    // 行高适应字体
    m_rxTable->verticalHeader()->setDefaultSectionSize(
        qRound(24 * scale));

    auto *rxStatusLayout = new QHBoxLayout();
    m_rxCountLabel = new QLabel("Rx: 0");
    m_autoScrollChk = new QCheckBox("自动滚动");
    m_autoScrollChk->setChecked(true);
    m_clearBtn = new QPushButton("清除");
    m_clearBtn->setFixedWidth(qRound(60 * scale));
    connect(m_clearBtn, &QPushButton::clicked, this, &CanSessionWidget::onClearClicked);

    rxStatusLayout->addWidget(m_rxCountLabel);
    rxStatusLayout->addStretch();
    rxStatusLayout->addWidget(m_autoScrollChk);
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
        if (checked && m_can->isOpen()) {
            m_periodicTimer->start(m_sendPeriodSpin->value());
        } else {
            m_periodicTimer->stop();
        }
    });
    connect(m_sendPeriodSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_periodicTimer->isActive())
            m_periodicTimer->setInterval(val);
    });

    m_sendBtn = new QPushButton("发送");
    m_sendBtn->setFixedWidth(qRound(80 * scale));
    m_sendBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #0b7dda; }");
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

// ─── 连接 / 断开 ──────────────────────────────────────────────

void CanSessionWidget::connectDevice(int channel, CanBaudRate baud)
{
    if (m_can->isOpen())
        disconnectDevice();

    if (m_can->open(channel, baud)) {
        m_statusLabel->setText("● 已连接");
        m_statusLabel->setStyleSheet("color:green; font-weight:bold;");
        m_connectBtn->setText("断开");
        m_connectBtn->setStyleSheet(
            "QPushButton { background-color: #f44336; color: white; font-weight: bold; "
            "border-radius: 3px; padding: 4px 12px; }"
            "QPushButton:hover { background-color: #d32f2f; }");
        m_deviceCombo->setEnabled(false);
        m_baudCombo->setEnabled(false);

        // 周期发送
        if (m_periodicSendChk->isChecked())
            m_periodicTimer->start(m_sendPeriodSpin->value());
    }
}

void CanSessionWidget::disconnectDevice()
{
    m_periodicTimer->stop();
    m_can->close();
    m_statusLabel->setText("未连接");
    m_statusLabel->setStyleSheet("color:gray; font-weight:bold;");
    m_connectBtn->setText("连接");
    m_connectBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #45a049; }");
    m_deviceCombo->setEnabled(true);
    m_baudCombo->setEnabled(true);
}

bool CanSessionWidget::isConnected() const
{
    return m_can && m_can->isOpen();
}

void CanSessionWidget::refreshDevices()
{
    QString current = m_deviceCombo->currentText();
    m_deviceCombo->clear();

    if (!m_pcan) return;
    QList<CanDeviceInfo> devices = m_pcan->scanDevices();

    if (devices.isEmpty()) {
        m_deviceCombo->addItem("未检测到设备", -1);
    } else {
        for (const auto &dev : devices) {
            m_deviceCombo->addItem(QString("%1  [通道 0x%2]")
                .arg(dev.name)
                .arg(dev.channel, 2, 16, QChar('0')), dev.channel);
        }
    }

    // 尝试恢复之前的选择
    int idx = m_deviceCombo->findText(current, Qt::MatchStartsWith);
    if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);
}

// ─── 槽 ───────────────────────────────────────────────────────

void CanSessionWidget::onConnectClicked()
{
    if (m_can->isOpen()) {
        disconnectDevice();
        return;
    }

    int channel = m_deviceCombo->currentData().toInt();
    if (channel <= 0) {
        QMessageBox::warning(this, "提示", "请先选择可用设备");
        return;
    }

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

    connectDevice(channel, baud);
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
    updateStats();
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
