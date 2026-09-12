#include "mockcanadapter.h"
#include <QDateTime>

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

QList<CanDeviceInfo> MockCanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;

    for (int i = 0; i < 2; ++i) {
        CanDeviceInfo info;
        info.name        = tr("MockCAN #%1").arg(i);
        info.description = tr("虚拟 CAN 通道 %1 (仅 Debug)").arg(i);
        info.channel     = i;
        info.adapterType = static_cast<int>(CanAdapterType::MockCan);
        devices.append(info);
    }

    return devices;
}

bool MockCanAdapter::open(int /*channel*/, CanBaudRate /*baud*/, CanDataBaudRate dataBaud)
{
    if (m_opened)
        close();

    m_opened  = true;
    m_dataBaud = dataBaud;
    m_msgCounter = 0;

    if (m_rxTimer->interval() <= 0)
        m_rxTimer->start(333);
    else
        m_rxTimer->start();

    return true;
}

void MockCanAdapter::close()
{
    m_rxTimer->stop();
    m_opened = false;
}

bool MockCanAdapter::isOpen() const
{
    return m_opened;
}

bool MockCanAdapter::isAlive() const
{
    return m_opened;
}

bool MockCanAdapter::sendMessage(const CanMessage &msg)
{
    Q_UNUSED(msg);
    return m_opened;
}

void MockCanAdapter::onRxTick()
{
    if (!m_opened)
        return;

    emit messageReceived(generateRandomMessage(0));
    emit messageReceived(generateRandomMessage(1));
}

CanMessage MockCanAdapter::generateRandomMessage(int channel)
{
    auto *rng = QRandomGenerator::global();

    CanMessage msg;
    msg.timestamp = QDateTime::currentDateTime();
    msg.direction = CanDirection::Rx;
    msg.channel   = channel;
    msg.isFd      = isFdEnabled();

    if (rng->bounded(10) < 2) { // 20% 扩展帧
        msg.id   = rng->bounded(0x1FFFFFFF);
        msg.type = CanFrameType::ExtendedData;
    } else {
        msg.id   = rng->bounded(0x7FF);
        msg.type = CanFrameType::StandardData;
    }

    if (msg.isFd) {
        // FD 只能使用 0~8/12/16/20/24/32/48/64 这些长度，随机取一个以覆盖长帧显示路径
        static const uint8_t fdLens[] = {8, 12, 16, 20, 24, 32, 48, 64};
        msg.dlc = fdLens[rng->bounded(int(sizeof(fdLens)))];
    } else {
        msg.dlc = static_cast<uint8_t>(rng->bounded(1, 9));
    }

    for (int i = 0; i < msg.dlc; ++i)
        msg.data[i] = static_cast<uint8_t>(rng->bounded(256));

    // 每隔几条输出一帧固定 ID 的递增数据，便于观察
    if (m_msgCounter % 5 == 0) {
        msg.id = 0x123;
        msg.type = CanFrameType::StandardData;
        msg.dlc = msg.isFd ? 64 : 8;
        for (int i = 0; i < msg.dlc; ++i)
            msg.data[i] = static_cast<uint8_t>((m_msgCounter + i) & 0xFF);
    }

    m_msgCounter++;
    return msg;
}

QList<int> MockCanAdapter::availableSendChannels() const
{
    QList<int> channels;
    if (m_opened)
        channels << 0 << 1;
    return channels;
}
