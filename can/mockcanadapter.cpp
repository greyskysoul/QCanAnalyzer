#include "mockcanadapter.h"
#include <QDateTime>

// ═══════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════

MockCanAdapter::MockCanAdapter(QObject *parent)
    : CanInterface(parent)
{
    m_rxTimer = new QTimer(this);
    m_rxTimer->setSingleShot(false);
    connect(m_rxTimer, &QTimer::timeout, this, &MockCanAdapter::onRxTick);
}

MockCanAdapter::~MockCanAdapter()
{
    close();
}

// ═══════════════════════════════════════════════════════════════
// 设备扫描
// ═══════════════════════════════════════════════════════════════

QList<CanDeviceInfo> MockCanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;

    // 模拟 2 个虚拟通道
    for (int i = 0; i < 2; ++i) {
        CanDeviceInfo info;
        info.name        = QString("MockCAN CH%1").arg(i);
        info.description = QString("虚拟 CAN 通道 %1 (仅 Debug)").arg(i);
        info.channel     = i;
        info.adapterType = static_cast<int>(CanAdapterType::MockCan);
        devices.append(info);
    }

    return devices;
}

// ═══════════════════════════════════════════════════════════════
// 打开 / 关闭
// ═══════════════════════════════════════════════════════════════

bool MockCanAdapter::open(int channel, CanBaudRate /*baud*/)
{
    if (m_opened)
        close();

    m_channel = channel;
    m_opened  = true;
    m_msgCounter = 0;

    // 默认每秒发送 3 条随机报文
    if (m_rxTimer->interval() == 0)
        m_rxTimer->start(333);
    else
        m_rxTimer->start();

    return true;
}

void MockCanAdapter::close()
{
    m_rxTimer->stop();
    m_opened = false;
    m_channel = 0;
}

bool MockCanAdapter::isOpen() const
{
    return m_opened;
}

bool MockCanAdapter::isAlive() const
{
    return m_opened; // 虚拟设备永远存活
}

// ═══════════════════════════════════════════════════════════════
// 发送
// ═══════════════════════════════════════════════════════════════

bool MockCanAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened)
        return false;

    // 模拟偶尔发送失败 (约 5% 概率)
    if (m_simulateError && QRandomGenerator::global()->bounded(100) < 5)
        return false;

    Q_UNUSED(msg);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// 自动接收
// ═══════════════════════════════════════════════════════════════

void MockCanAdapter::setAutoRxInterval(int ms)
{
    if (ms <= 0) {
        m_rxTimer->stop();
    } else {
        m_rxTimer->start(ms);
    }
}

void MockCanAdapter::onRxTick()
{
    if (!m_opened)
        return;

    CanMessage msg = generateRandomMessage();
    emit messageReceived(msg);
}

CanMessage MockCanAdapter::generateRandomMessage()
{
    auto *rng = QRandomGenerator::global();

    CanMessage msg;
    msg.timestamp = QDateTime::currentDateTime();
    msg.direction = CanDirection::Rx;
    msg.channel   = m_channel;
    msg.isFd      = false;

    // 随机 ID (11-bit 或 29-bit)
    if (rng->bounded(10) < 2) {
        // 20% 概率生成扩展帧
        msg.id   = rng->bounded(0x1FFFFFFF);
        msg.type = CanFrameType::ExtendedData;
    } else {
        msg.id   = rng->bounded(0x7FF);
        msg.type = CanFrameType::StandardData;
    }

    // 随机 DLC (0~8)
    msg.dlc = static_cast<uint8_t>(rng->bounded(1, 9));

    // 随机数据
    for (int i = 0; i < msg.dlc; ++i) {
        msg.data[i] = static_cast<uint8_t>(rng->bounded(256));
    }

    // 每隔几条使数据有规律递增，便于观察
    if (m_msgCounter % 5 == 0) {
        msg.id = 0x123;
        msg.type = CanFrameType::StandardData;
        msg.dlc = 8;
        msg.isFd = false;
        for (int i = 0; i < 8; ++i)
            msg.data[i] = static_cast<uint8_t>((m_msgCounter + i) & 0xFF);
    }

    m_msgCounter++;
    return msg;
}
