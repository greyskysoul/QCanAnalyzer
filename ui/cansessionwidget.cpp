#include "cansessionwidget.h"
#include "ui_cansessionwidget.h"
#include <QEvent>
#ifndef Q_OS_LINUX
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#include "can/zcanfdadapter.h"
#include "can/zcanadapter.h"
#else
#include "can/socketcanadapter.h"
#include "can/zcanfdadapter.h"
#endif
#ifdef QT_DEBUG
#include "can/mockcanadapter.h"
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
#include "qhexedit.h"

namespace {

QString buttonStyle(const char *background, const char *hover)
{
    return QStringLiteral(
        "QPushButton { font-weight: bold; border-radius: 3px; padding: 4px 10px;"
        " background-color: %1; color: white; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(QString::fromLatin1(background), QString::fromLatin1(hover));
}

const char *const kGreen      = "#4CAF50";
const char *const kGreenHover = "#45a049";
const char *const kRed        = "#f44336";
const char *const kRedHover   = "#d32f2f";
const char *const kBlue       = "#2196F3";
const char *const kBlueHover  = "#0b7dda";
const char *const kGray       = "#607d8b";
const char *const kGrayHover  = "#455a64";

} // namespace

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
    // 不调 disconnectDevice(): 它会访问 UI 控件，而此处 UI 可能已部分销毁
    m_statusTimer->stop();
    m_frameTimer->stop();
    if (m_can)
        m_can->close();
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
        ui->statusLabel->setText(tr("⚠ %1").arg(shortErr));
        ui->statusLabel->setToolTip(err);
        ui->statusLabel->setStyleSheet("color:orange; font-weight:bold;");
    });
}

void CanSessionWidget::setupUi()
{
    qreal scale = QApplication::primaryScreen()->devicePixelRatio();

    ui->deviceLabel->setStyleSheet("font-weight:bold; color:#2c3e50;");
    ui->deviceLabel->setMinimumWidth(qRound(120 * scale));

    ui->baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    ui->baudCombo->setCurrentText("500K");

    // 数据域波特率项由枚举派生，避免与 dataBaudRateString() 的字符串脱节
    for (CanDataBaudRate r : selectableDataBaudRates())
        ui->dataBaudCombo->addItem(dataBaudRateString(r));
    ui->dataBaudCombo->setCurrentText(dataBaudRateString(CanDataBaudRate::BR_2M));
    ui->dataBaudLabel->setVisible(false);
    ui->dataBaudCombo->setVisible(false);

    ui->connectBtn->setFixedWidth(qRound(70 * scale));
    ui->connectBtn->setStyleSheet(buttonStyle(kGreen, kGreenHover));
    connect(ui->connectBtn, &QPushButton::clicked, this, &CanSessionWidget::onConnectClicked);

    ui->statusLabel->setStyleSheet("color:gray; font-weight:bold;");
    ui->statusLabel->setMinimumWidth(qRound(120 * scale));

    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColTime, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColTime, 100);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColDir, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColDir, 50);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColId, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColId, 100);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColCh, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColCh, 45);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColType, 60);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColDlc, QHeaderView::Fixed);
    ui->rxTable->horizontalHeader()->resizeSection(ColDlc, 45);
    ui->rxTable->horizontalHeader()->setSectionResizeMode(ColData, QHeaderView::Stretch);
    ui->rxTable->horizontalHeader()->setStretchLastSection(false);
    ui->rxTable->verticalHeader()->setDefaultSectionSize(24);

    ui->saveBtn->setFixedWidth(qRound(55 * scale));
    ui->saveBtn->setStyleSheet(buttonStyle(kGray, kGrayHover));
    connect(ui->saveBtn, &QPushButton::clicked, this, &CanSessionWidget::onSaveClicked);

    ui->clearBtn->setFixedWidth(qRound(55 * scale));
    ui->clearBtn->setStyleSheet(buttonStyle(kGray, kGrayHover));
    connect(ui->clearBtn, &QPushButton::clicked, this, &CanSessionWidget::onClearClicked);

    ui->sendIdEdit->setMaximumWidth(qRound(100 * scale));
    ui->sendTypeCombo->addItem(tr("标准数据帧"));
    ui->sendTypeCombo->addItem(tr("扩展数据帧"));
    ui->sendTypeCombo->addItem(tr("远程帧"));
    ui->sendDlcSpin->setRange(0, 8);
    ui->sendDlcSpin->setValue(8);
    ui->sendDlcSpin->setFixedWidth(qRound(55 * scale));

    // 用 QHexEdit 替换 .ui 中的占位控件
    m_sendDataEdit = new QHexEdit(this);
    m_sendDataEdit->setOverwriteMode(true);
    m_sendDataEdit->setReadOnly(false);
    m_sendDataEdit->setAddressArea(true);
    m_sendDataEdit->setAddressWidth(2);
    m_sendDataEdit->setAsciiArea(true);
    m_sendDataEdit->setBytesPerLine(8);
    m_sendDataEdit->setHexCaps(true);
    if (scale > 1.0f)
        m_sendDataEdit->setFixedWidth(qRound(155 * scale));
    else
        m_sendDataEdit->setFixedWidth(305);
    m_sendDataEdit->setData(QByteArray(8, '\0'));
    ui->txDataLayout->removeWidget(ui->sendDataEditHolder);
    ui->sendDataEditHolder->hide();
    ui->txDataLayout->insertWidget(1, m_sendDataEdit);

    connect(ui->sendDlcSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CanSessionWidget::onSendDlcChanged);

    ui->sendChanCombo->setFixedWidth(qRound(60 * scale));
    connect(ui->sendChanCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanSessionWidget::onSendChannelChanged);

    // 0 = 最快 (1ms 定时器)，>0 = 每帧间隔 N ms
    ui->sendPeriodSpin->setRange(0, 10000);
    ui->sendPeriodSpin->setValue(0);
    ui->sendPeriodSpin->setSpecialValueText(tr("最快"));
    ui->sendPeriodSpin->setSuffix(tr(" ms"));

    ui->sendFrameCountSpin->setMinimum(1);
    ui->sendFrameCountSpin->setMaximum(999999);
    ui->sendFrameCountSpin->setValue(1);
    ui->sendFrameCountSpin->setFixedWidth(qRound(70 * scale));

    ui->sendBtn->setFixedWidth(qRound(80 * scale));
    ui->sendBtn->setStyleSheet(buttonStyle(kBlue, kBlueHover));
    connect(ui->sendBtn, &QPushButton::clicked, this, &CanSessionWidget::onSendClicked);

    ui->splitter->setStretchFactor(0, 3);
    ui->splitter->setStretchFactor(1, 1);

    connect(ui->filterIdEdit, &QLineEdit::textChanged,
            this, &CanSessionWidget::onFilterChanged);
    connect(ui->filterPassChk, &QCheckBox::toggled,
            this, &CanSessionWidget::onFilterChanged);
    onFilterChanged();
}

void CanSessionWidget::connectDevice(int channel, CanBaudRate baud, int adapterType,
                                     CanDataBaudRate dataBaud)
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
    case CanAdapterType::ZCANFD:
        if (!m_zcanfd) { m_zcanfd = new ZcanFdAdapter(this); linkSignals(m_zcanfd); }
        newCan = m_zcanfd;
        break;
#ifndef Q_OS_LINUX
    case CanAdapterType::ZCAN:
        if (!m_zcan) { m_zcan = new ZcanAdapter(this); linkSignals(m_zcan); }
        newCan = m_zcan;
        break;
#endif
#ifdef Q_OS_LINUX
    case CanAdapterType::SocketCAN:
        if (!m_socketcan) { m_socketcan = new SocketCanAdapter(this); linkSignals(m_socketcan); }
        newCan = m_socketcan;
        break;
#endif
#ifdef QT_DEBUG
    case CanAdapterType::MockCan:
        if (!m_mockcan) { m_mockcan = new MockCanAdapter(this); linkSignals(m_mockcan); }
        newCan = m_mockcan;
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
        ui->deviceLabel->setText(tr("SocketCAN (请用 ip link 命令设置波特率)"));
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

    if (m_can->open(channel, baud, dataBaud)) {
        updateUiState(true);
        refreshSendChannelCombo();
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

void CanSessionWidget::refreshSendChannelCombo()
{
    ui->sendChanCombo->blockSignals(true);
    ui->sendChanCombo->clear();

    if (!m_can || !m_can->isOpen()) {
        ui->sendChanCombo->setEnabled(false);
        ui->sendChanCombo->blockSignals(false);
        return;
    }

    QList<int> channels = m_can->availableSendChannels();
    int currentCh = m_can->currentSendChannel();

    for (int ch : channels) {
        ui->sendChanCombo->addItem(tr("CH%1").arg(ch), ch);
        if (ch == currentCh)
            ui->sendChanCombo->setCurrentIndex(ui->sendChanCombo->count() - 1);
    }

    ui->sendChanCombo->setEnabled(channels.size() > 1);
    ui->sendChanCombo->blockSignals(false);
}

void CanSessionWidget::onSendChannelChanged(int index)
{
    if (!m_can || index < 0) return;
    int ch = ui->sendChanCombo->itemData(index).toInt();
    m_can->setSendChannel(ch);
    m_channelIndex = ch;
}

void CanSessionWidget::onStatusCheck()
{
    if (!m_can || !m_can->isOpen()) return;

    if (!m_can->isAlive()) {
        m_statusTimer->stop();
        stopSending();
        m_can->close();
        updateUiState(false);

        ui->statusLabel->setText(tr("⚠ 设备已断开"));
        ui->statusLabel->setStyleSheet("color:red; font-weight:bold;");
        emit deviceDisconnected(m_sessionId);
    }
}

void CanSessionWidget::updateUiState(bool connected)
{
    if (connected) {
        ui->statusLabel->setText(tr("● 已连接"));
        ui->statusLabel->setToolTip("");
        ui->statusLabel->setStyleSheet("color:green; font-weight:bold;");
        ui->connectBtn->setText(tr("断开"));
        ui->connectBtn->setStyleSheet(buttonStyle(kRed, kRedHover));
        ui->baudCombo->setEnabled(false);
        ui->dataBaudCombo->setEnabled(false);
        ui->sendChanCombo->setEnabled(m_can && m_can->availableSendChannels().size() > 1);
    } else {
        ui->statusLabel->setText(tr("未连接"));
        ui->statusLabel->setToolTip("");
        ui->statusLabel->setStyleSheet("color:gray; font-weight:bold;");
        ui->connectBtn->setText(tr("连接"));
        ui->connectBtn->setStyleSheet(buttonStyle(kGreen, kGreenHover));
        ui->baudCombo->setEnabled(true);
        ui->dataBaudCombo->setEnabled(true);
        ui->sendChanCombo->clear();
        ui->sendChanCombo->setEnabled(false);

        qDeleteAll(m_channelChks);
        m_channelChks.clear();
    }
}

void CanSessionWidget::updateChannelCheckboxes()
{
    qDeleteAll(m_channelChks);
    m_channelChks.clear();

    if (!m_can || !m_can->isOpen()) return;

    QList<int> channels = m_can->availableSendChannels();
    for (int ch : channels) {
        auto *chk = new QCheckBox(tr("CH%1").arg(ch));
        chk->setChecked(true);
        chk->setToolTip(tr("通道 %1").arg(ch));
        chk->setProperty("canChannel", ch);
        m_channelChks.append(chk);
        ui->channelChkLayout->addWidget(chk);
    }
    ui->channelChkLayout->setAlignment(Qt::AlignLeft);
    ui->channelChkLayout->addStretch();
}

void CanSessionWidget::onConnectClicked()
{
    if (m_can->isOpen()) {
        disconnectDevice();
        return;
    }

    if (m_currentChannel >= 0) {
        CanBaudRate baud = baudRateFromString(ui->baudCombo->currentText());

        connectDevice(m_currentChannel, baud, m_adapterType, dataBaudRate());
    }
}

void CanSessionWidget::onSendClicked()
{
    if (!m_can->isOpen()) return;

    if (m_sending) {
        stopSending();
        return;
    }

    prepareAndStartSend();
}

void CanSessionWidget::prepareAndStartSend()
{
    if (!m_can->isOpen()) return;

    QByteArray rawData = m_sendDataEdit->data();
    int dataLen = qMin(rawData.size(), 64);

    // setData 会清除 QHexEdit 的 modified 高亮
    m_sendDataEdit->setData(rawData);

    m_pendingMsg = CanMessage();
    m_pendingMsg.direction = CanDirection::Tx;
    if (m_can->currentSendChannel() >= 0)
        m_pendingMsg.channel = m_can->currentSendChannel();
    else
        m_pendingMsg.channel = m_channelIndex;

    QString idText = ui->sendIdEdit->text().trimmed();
    bool ok = false;
    if (idText.startsWith("0x", Qt::CaseInsensitive))
        m_pendingMsg.id = idText.mid(2).toUInt(&ok, 16);
    else
        m_pendingMsg.id = idText.toUInt(&ok, 16);
    if (!ok) m_pendingMsg.id = 0x123;

    int typeIdx = ui->sendTypeCombo->currentIndex();
    if (typeIdx == 0) m_pendingMsg.type = CanFrameType::StandardData;
    else if (typeIdx == 1) m_pendingMsg.type = CanFrameType::ExtendedData;
    else m_pendingMsg.type = CanFrameType::Remote;

    int maxDlc = m_isCanFd ? 64 : 8;
    m_pendingMsg.dlc = static_cast<uint8_t>(qMin(qMax(dataLen, ui->sendDlcSpin->value()), maxDlc));
    m_pendingMsg.isFd = m_isCanFd && (m_pendingMsg.dlc > 8);
    // CAN FD 只允许 0~8/12/16/20/24/32/48/64 这些长度
    if (m_pendingMsg.isFd)
        m_pendingMsg.dlc = static_cast<uint8_t>(canFdSnapLen(m_pendingMsg.dlc));

    for (int i = 0; i < dataLen; ++i)
        m_pendingMsg.data[i] = static_cast<uint8_t>(rawData[i]);

    m_frameRemaining = ui->sendFrameCountSpin->value();
    m_sending = true;

    int intervalMs = qMax(ui->sendPeriodSpin->value(), 1);

    // 单帧 + 最快模式不必切换成“停止”，避免按钮一闪而过
    if (m_frameRemaining > 1 || intervalMs > 1)
        updateSendButtonState(true);

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

    if (--m_frameRemaining <= 0)
        stopSending();
}

void CanSessionWidget::stopSending()
{
    m_frameTimer->stop();
    m_sending = false;
    m_frameRemaining = 0;

    if (m_sendUiActive)
        updateSendButtonState(false);
}

void CanSessionWidget::updateSendButtonState(bool sending)
{
    m_sendUiActive = sending;

    const qreal scale = QApplication::primaryScreen()->devicePixelRatio();

    ui->sendBtn->setText(sending ? tr("停止") : tr("发送"));
    ui->sendBtn->setStyleSheet(sending ? buttonStyle(kRed, kRedHover)
                                       : buttonStyle(kBlue, kBlueHover));
    ui->sendBtn->setFixedWidth(qRound(80 * scale));

    const bool editable = !sending;
    ui->sendIdEdit->setEnabled(editable);
    ui->sendTypeCombo->setEnabled(editable);
    ui->sendDlcSpin->setEnabled(editable);
    m_sendDataEdit->setEnabled(editable);
    ui->sendFrameCountSpin->setEnabled(editable);
    ui->sendPeriodSpin->setEnabled(editable);
    ui->sendChanCombo->setEnabled(editable && m_can
                                  && m_can->availableSendChannels().size() > 1);
}

void CanSessionWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void CanSessionWidget::retranslateDynamicUi()
{
    // retranslateUi 会清空代码填充的下拉框内容，需要重建
    int typeIdx = ui->sendTypeCombo->currentIndex();
    ui->sendTypeCombo->clear();
    ui->sendTypeCombo->addItem(tr("标准数据帧"));
    ui->sendTypeCombo->addItem(tr("扩展数据帧"));
    ui->sendTypeCombo->addItem(tr("远程帧"));
    ui->sendTypeCombo->setCurrentIndex(typeIdx);

    ui->sendPeriodSpin->setSpecialValueText(tr("最快"));
    ui->sendPeriodSpin->setSuffix(tr(" ms"));

    // 设备名（retranslateUi 会重置为 .ui 中的 “—”），以及 CAN-FD 时的波特率标签
#ifdef Q_OS_LINUX
    if (m_adapterType == static_cast<int>(CanAdapterType::SocketCAN))
        ui->deviceLabel->setText(tr("SocketCAN (请用 ip link 命令设置波特率)"));
    else if (m_can)
        ui->deviceLabel->setText(m_can->adapterName());
#else
    if (m_can)
        ui->deviceLabel->setText(m_can->adapterName());
#endif
    if (m_isCanFd)
        ui->connBaudPrefixLabel->setText(tr("仲裁域:"));

    updateUiState(m_can && m_can->isOpen());
    updateSendButtonState(m_sendUiActive);
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
        this, tr("保存 CAN 报文"), defaultName,
        "CSV 文件 (*.csv);;所有文件 (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QChar(0xFEFF);

    out << tr("时间,方向,ID,通道,类型,DLC,数据") << "\n";

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

void CanSessionWidget::onFilterChanged()
{
    bool pass = ui->filterPassChk->isChecked();
    m_filterEnabled = !pass;
    m_filterEntries.clear();

    if (pass) {
        ui->filterIdEdit->setEnabled(false);
        ui->filterIdEdit->setStyleSheet("");
        return;
    }

    ui->filterIdEdit->setEnabled(true);

    QString text = ui->filterIdEdit->text().trimmed();
    if (text.isEmpty()) {
        m_filterEnabled = false;
        ui->filterIdEdit->setStyleSheet("");
        return;
    }

    // 默认十六进制，逗号分隔；支持 ID 精确匹配与 ID-Mask 两种写法：
    //   123     → (id & 0xFFFFFFFF) == 0x123
    //   123-FF0 → (id & 0xFF0) == (0x123 & 0xFF0)
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    bool parseOk = true;
    for (const QString &part : parts) {
        QString s = part.trimmed().toUpper();
        if (s.isEmpty()) continue;

        uint32_t mask = 0xFFFFFFFF;
        QString idPart = s;
        int dashIdx = s.indexOf('-');
        if (dashIdx > 0) {
            idPart = s.left(dashIdx).trimmed();
            bool okMask = false;
            mask = s.mid(dashIdx + 1).trimmed().toUInt(&okMask, 16);
            if (!okMask) { parseOk = false; continue; }
        }

        bool okId = false;
        uint32_t id = idPart.toUInt(&okId, 16);
        if (!okId) { parseOk = false; continue; }

        FilterEntry e;
        e.id = id;
        e.mask = mask;
        m_filterEntries.append(e);
    }

    if (m_filterEntries.isEmpty()) {
        m_filterEnabled = false;
        ui->filterIdEdit->setStyleSheet(parseOk ? "" : "border: 1px solid red;");
    } else if (!parseOk) {
        ui->filterIdEdit->setStyleSheet("border: 1px solid orange;");
    } else {
        ui->filterIdEdit->setStyleSheet("border: 1px solid #4CAF50;");
    }
}

bool CanSessionWidget::passFilter(const CanMessage &msg) const
{
    if (!m_filterEnabled || m_filterEntries.isEmpty())
        return true;

    for (const FilterEntry &e : m_filterEntries) {
        if ((msg.id & e.mask) == (e.id & e.mask))
            return true;
    }
    return false;
}

void CanSessionWidget::onMessageReceived(const CanMessage &msg)
{
    if (!passFilter(msg)) return;

    // 未使能的通道不显示
    for (const QCheckBox *chk : m_channelChks) {
        bool ok = false;
        int ch = chk->property("canChannel").toInt(&ok);
        if (ok && ch == msg.channel && !chk->isChecked())
            return;
    }

    m_rxCount++;
    addMessageToTable(msg);
}

void CanSessionWidget::addMessageToTable(const CanMessage &msg)
{
    int row = ui->rxTable->rowCount();

    if (row >= kMaxTableRows) {
        ui->rxTable->removeRow(0);
        row--;
    }

    ui->rxTable->insertRow(row);

    auto *timeItem = new QTableWidgetItem(msg.timestamp.toString("hh:mm:ss.zzz"));
    timeItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColTime, timeItem);

    auto *dirItem = new QTableWidgetItem(msg.direction == CanDirection::Rx ? "Rx" : "Tx");
    dirItem->setTextAlignment(Qt::AlignCenter);
    dirItem->setForeground(msg.direction == CanDirection::Rx ? QColor("#2196F3") : QColor("#4CAF50"));
    ui->rxTable->setItem(row, ColDir, dirItem);

    auto *idItem = new QTableWidgetItem(msg.idString());
    idItem->setTextAlignment(Qt::AlignCenter);
    if (msg.type == CanFrameType::ExtendedData)
        idItem->setForeground(QColor("#E91E63"));
    ui->rxTable->setItem(row, ColId, idItem);

    auto *chItem = new QTableWidgetItem(QString("CH%1").arg(msg.channel));
    chItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColCh, chItem);

    QString typeStr = msg.typeString();
    auto *typeItem = new QTableWidgetItem(typeStr);
    typeItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColType, typeItem);

    auto *dlcItem = new QTableWidgetItem(QString::number(msg.dlc));
    dlcItem->setTextAlignment(Qt::AlignCenter);
    ui->rxTable->setItem(row, ColDlc, dlcItem);

    auto *dataItem = new QTableWidgetItem(msg.dataHex());
    dataItem->setFont(QFont("Consolas", 9));
    if (msg.dlc > 8) {
        // CAN FD 长数据：每 8 字节换行
        QString text = msg.dataHex();
        QString wrapped;
        int byteCnt = 0;
        for (int i = 0; i < text.length(); ++i) {
            wrapped += text[i];
            if (text[i] == ' ') {
                byteCnt++;
                if (byteCnt == 8 && i + 1 < text.length()) {
                    wrapped += '\n';
                    byteCnt = 0;
                }
            }
        }
        dataItem->setText(wrapped.trimmed());
    }
    ui->rxTable->setItem(row, ColData, dataItem);

    if (msg.dlc > 8) {
        int lines = (msg.dlc + 7) / 8;
        ui->rxTable->verticalHeader()->resizeSection(row, 18 * lines);
    }

    if (ui->autoScrollChk->isChecked())
        ui->rxTable->scrollToBottom();

    updateStats();
}

void CanSessionWidget::updateStats()
{
    ui->rxCountLabel->setText(tr("Rx: %1  |  Tx: %2").arg(m_rxCount).arg(m_txCount));
}

void CanSessionWidget::setCanFdEnabled(bool enabled)
{
    if (m_isCanFd == enabled)
        return;
    ui->canFdChk->setChecked(enabled);
    onCanFdToggled(enabled);
}

void CanSessionWidget::setBaudRateText(const QString &text)
{
    ui->baudCombo->setCurrentText(text);
}

void CanSessionWidget::setDataBaudRate(CanDataBaudRate dataBaud)
{
    const QString text = dataBaudRateString(dataBaud);
    if (!text.isEmpty())
        ui->dataBaudCombo->setCurrentText(text);
}

CanDataBaudRate CanSessionWidget::dataBaudRate() const
{
    if (!m_isCanFd)
        return CanDataBaudRate::None;
    return dataBaudRateFromString(ui->dataBaudCombo->currentText());
}

void CanSessionWidget::onSendDlcChanged(int dlc)
{
    if (!m_sendDataEdit) return;

    if (!m_isCanFd && dlc > 8)
        dlc = 8;

    QByteArray current = m_sendDataEdit->data();
    if (current.size() != dlc) {
        current.resize(dlc); // 新增字节为 0x00
        m_sendDataEdit->setData(current);
    }
}

void CanSessionWidget::onCanFdToggled(bool checked)
{
    m_isCanFd = checked;

    ui->connBaudPrefixLabel->setText(checked ? tr("仲裁域:") : tr("波特率:"));
    ui->dataBaudLabel->setVisible(checked);
    ui->dataBaudCombo->setVisible(checked);

    ui->sendDlcSpin->blockSignals(true);
    if (checked) {
        ui->sendDlcSpin->setRange(0, 64);
        ui->sendDlcSpin->setValue(64);
    } else {
        ui->sendDlcSpin->setRange(0, 8);
        if (ui->sendDlcSpin->value() > 8)
            ui->sendDlcSpin->setValue(8);
    }
    ui->sendDlcSpin->blockSignals(false);

    onSendDlcChanged(ui->sendDlcSpin->value());
}
