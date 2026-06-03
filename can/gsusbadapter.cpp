#include "gsusbadapter.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>

// candle API 头文件
extern "C" {
#include "CandleApiDriver/api/candle.h"
}

GsUsbAdapter::GsUsbAdapter(QObject *parent)
    : CanInterface(parent)
{
}

GsUsbAdapter::~GsUsbAdapter()
{
    close();
}

QList<CanDeviceInfo> GsUsbAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;

    // 使用 candle API 扫描
    candle_list_handle list = nullptr;
    if (!candle_list_scan(&list) || !list)
        return devices;

    uint8_t count = 0;
    candle_list_length(list, &count);

    for (uint8_t i = 0; i < count; ++i) {
        candle_handle hdev = nullptr;
        if (!candle_dev_get(list, i, &hdev) || !hdev)
            continue;

        uint8_t numChannels = 0;
        candle_channel_count(hdev, &numChannels);

        wchar_t *path = candle_dev_get_path(hdev);
        QString pathStr = QString::fromWCharArray(path);

        // 每个通道作为一个设备
        for (uint8_t ch = 0; ch < numChannels; ++ch) {
            CanDeviceInfo info;
            info.channel = (i << 8) | ch; // 编码: 高字节=设备号, 低字节=通道号
            info.adapterType = static_cast<int>(CanAdapterType::GsUsb);
            info.name = QString("candleLight #%1 CH%2").arg(i).arg(ch);
            info.description = QString("%1 [%2]").arg(info.name).arg(pathStr);
            devices.append(info);
        }

        candle_dev_free(hdev);
    }

    candle_list_free(list);

    return devices;
}

bool GsUsbAdapter::open(int channel, CanBaudRate baud)
{
    if (m_opened) close();

    // 使用 candle API 打开
    candle_list_handle list = nullptr;
    if (!candle_list_scan(&list) || !list) {
        emit errorOccurred("未找到 candleLight 设备");
        return false;
    }

    uint8_t devIndex = (channel >> 8) & 0xFF;
    uint8_t ch = channel & 0xFF;

    uint8_t count = 0;
    candle_list_length(list, &count);
    if (devIndex >= count) {
        candle_list_free(list);
        emit errorOccurred("设备索引超出范围");
        return false;
    }

    candle_handle hdev = nullptr;
    if (!candle_dev_get(list, devIndex, &hdev) || !hdev) {
        candle_list_free(list);
        emit errorOccurred("获取设备句柄失败");
        return false;
    }

    if (!candle_dev_open(hdev)) {
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred("打开 candleLight 设备失败");
        return false;
    }

    // 设置波特率
    uint32_t bitrate = 500000;
    switch (baud) {
    case CanBaudRate::BR_1M:   bitrate = 1000000; break;
    case CanBaudRate::BR_800K: bitrate = 800000;  break;
    case CanBaudRate::BR_500K: bitrate = 500000;  break;
    case CanBaudRate::BR_250K: bitrate = 250000;  break;
    case CanBaudRate::BR_125K: bitrate = 125000;  break;
    case CanBaudRate::BR_100K: bitrate = 100000;  break;
    case CanBaudRate::BR_50K:  bitrate = 50000;   break;
    case CanBaudRate::BR_20K:  bitrate = 20000;   break;
    case CanBaudRate::BR_10K:  bitrate = 10000;   break;
    case CanBaudRate::BR_5K:   bitrate = 5000;    break;
    default: break;
    }
    candle_channel_set_bitrate(hdev, ch, bitrate);
    candle_channel_start(hdev, ch, 0);

    m_devHandle = hdev;
    m_devList = list;
    m_channelCount = 1;
    m_opened = true;

    // 读取线程
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, ch]() {
        if (!m_opened || !m_devHandle) return;
        candle_frame_t frame;
        while (candle_frame_read(static_cast<candle_handle>(m_devHandle), &frame, 0)) {
            if (candle_frame_type(&frame) == CANDLE_FRAMETYPE_RECEIVE) {
                CanMessage msg;
                msg.id = candle_frame_id(&frame);
                msg.dlc = candle_frame_dlc(&frame);
                msg.direction = CanDirection::Rx;
                msg.timestamp = QDateTime::currentDateTime();
                msg.type = candle_frame_is_extended_id(&frame)
                    ? CanFrameType::ExtendedData : CanFrameType::StandardData;

                uint8_t *data = candle_frame_data(&frame);
                for (uint8_t i = 0; i < msg.dlc && i < 8; ++i)
                    msg.data[i] = data[i];

                emit messageReceived(msg);
            }
        }
    });
    timer->start(1);

    return true;
}

void GsUsbAdapter::close()
{
    if (!m_opened) return;

    if (m_devHandle) {
        candle_dev_close(static_cast<candle_handle>(m_devHandle));
        candle_dev_free(static_cast<candle_handle>(m_devHandle));
        m_devHandle = nullptr;
    }
    if (m_devList) {
        candle_list_free(static_cast<candle_list_handle>(m_devList));
        m_devList = nullptr;
    }
    m_opened = false;
}

bool GsUsbAdapter::isOpen() const
{
    return m_opened;
}

bool GsUsbAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened || !m_devHandle) return false;

    candle_frame_t frame = {};
    frame.can_id = msg.id;
    if (msg.type == CanFrameType::ExtendedData)
        frame.can_id |= CANDLE_ID_EXTENDED;
    frame.can_dlc = msg.dlc > 64 ? 64 : msg.dlc;
    for (uint8_t i = 0; i < frame.can_dlc; ++i)
        frame.data[i] = msg.data[i];

    return candle_frame_send(static_cast<candle_handle>(m_devHandle),
                             0, &frame);
}

bool GsUsbAdapter::isAlive() const
{
    return m_opened;
}

QString GsUsbAdapter::channelName(int channel)
{
    int dev = (channel >> 8) & 0xFF;
    int ch = channel & 0xFF;
    return QString("candleLight #%1 CH%2").arg(dev).arg(ch);
}

