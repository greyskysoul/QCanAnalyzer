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
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
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
    // 设置波特率 — 优先使用简单API，失败则用 bittiming 方式
    qDebug() << "gs_usb: setting bitrate" << bitrate << "for channel" << ch;
    bool bitrateOk = candle_channel_set_bitrate(hdev, ch, bitrate);
    if (!bitrateOk) {
        candle_err_t err = candle_dev_last_error(hdev);
        qWarning() << "gs_usb: candle_channel_set_bitrate failed, err=" << (int)err
                   << "— falling back to bittiming";

        // 获取设备能力
        candle_capability_t caps;
        if (!candle_channel_get_capabilities(hdev, ch, &caps)) {
            candle_dev_free(hdev);
            candle_list_free(list);
            emit errorOccurred("gs_usb: 无法获取设备能力");
            return false;
        }

        qDebug() << "gs_usb: fclk_can=" << caps.fclk_can
                 << "tseg1=[" << caps.tseg1_min << "," << caps.tseg1_max << "]"
                 << "tseg2=[" << caps.tseg2_min << "," << caps.tseg2_max << "]"
                 << "sjw_max=" << caps.sjw_max
                 << "brp=[" << caps.brp_min << "," << caps.brp_max << "]";

        // 遍历寻找合适的 bittiming（采样点 70%~87.5%）
        candle_bittiming_t best = {};
        bool found = false;

        for (uint32_t brp = caps.brp_min; brp <= caps.brp_max && !found; brp += caps.brp_inc) {
            uint32_t tq_total = caps.fclk_can / (brp * bitrate);
            if (tq_total < 4 || tq_total > 25) continue; // CAN 规范: 最小 4TQ, 最大 25TQ

            // tq_total = 1 + tseg1 + tseg2 (prop_seg 已合并到 tseg1)
            for (uint32_t tseg2 = caps.tseg2_min; tseg2 <= caps.tseg2_max && tseg2 < tq_total; tseg2++) {
                uint32_t tseg1 = tq_total - 1 - tseg2;
                if (tseg1 < caps.tseg1_min || tseg1 > caps.tseg1_max) continue;

                // 采样点 = (1 + tseg1) / tq_total, 目标 70%~87.5%
                uint32_t sp = (1 + tseg1) * 1000 / tq_total;
                if (sp >= 700 && sp <= 875) {
                    best.prop_seg = 1;
                    best.phase_seg1 = tseg1 - 1; // tseg1 = prop_seg + phase_seg1
                    best.phase_seg2 = tseg2;
                    best.sjw = (caps.sjw_max < tseg2) ? caps.sjw_max : tseg2;
                    best.brp = brp;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            candle_dev_free(hdev);
            candle_list_free(list);
            emit errorOccurred(QString("gs_usb: 无法为 bitrate=%1 找到合适的 bittiming (fclk=%2)")
                               .arg(bitrate).arg(caps.fclk_can));
            return false;
        }

        qDebug() << "gs_usb: bittiming brp=" << best.brp
                 << "prop=" << best.prop_seg
                 << "ps1=" << best.phase_seg1
                 << "ps2=" << best.phase_seg2
                 << "sjw=" << best.sjw;

        if (!candle_channel_set_timing(hdev, ch, &best)) {
            candle_err_t err2 = candle_dev_last_error(hdev);
            candle_dev_free(hdev);
            candle_list_free(list);
            emit errorOccurred(QString("gs_usb: 设置 bittiming 失败 (err=%1)").arg(static_cast<int>(err2)));
            return false;
        }
        qDebug() << "gs_usb: bittiming set OK";
    } else {
        qDebug() << "gs_usb: bitrate set OK via simple API";
    }

    // 普通模式启动 CAN 通道（需要总线至少两个节点才能正常 ACK）
    uint32_t flags = 0;
    qDebug() << "gs_usb: starting channel" << ch << "flags=0x" << Qt::hex << flags;
    if (!candle_channel_start(hdev, ch, flags)) {
        candle_err_t err = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 启动通道失败 (err=%1)").arg(static_cast<int>(err)));
        return false;
    }
    qDebug() << "gs_usb: channel started OK";

    m_devHandle = hdev;
    m_devList = list;
    m_channelCount = 1;
    m_channelIndex = ch;
    m_opened = true;
    m_deviceLost = false;

    // 读取定时器 — 先停止并删除旧的，再创建新的
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    m_readTimer = new QTimer(this);
    connect(m_readTimer, &QTimer::timeout, this, [this, ch]() {
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
    m_readTimer->start(1);

    return true;
}

void GsUsbAdapter::close()
{
    // 先停止读取定时器
    if (m_readTimer) {
        m_readTimer->stop();
    }

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
    m_deviceLost = false;
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
    else if (msg.type == CanFrameType::Remote)
        frame.can_id |= CANDLE_ID_RTR;
    frame.can_dlc = msg.dlc > 8 ? 8 : msg.dlc;
    if (msg.dlc > 8) {
        qWarning() << "gs_usb: DLC truncated from" << msg.dlc << "to 8";
    }
    for (uint8_t i = 0; i < frame.can_dlc; ++i)
        frame.data[i] = msg.data[i];

    // 调试: 打印发送的原始帧数据
    uint8_t *raw = (uint8_t*)&frame;
    QString hex;
    for (int i = 0; i < 24; ++i)
        hex += QString("%1 ").arg(raw[i], 2, 16, QChar('0'));
    qDebug() << "gs_usb TX raw[24]:" << hex.trimmed();

    bool ret = candle_frame_send(static_cast<candle_handle>(m_devHandle),
                                 m_channelIndex, &frame);
    if (!ret) {
        candle_err_t err = candle_dev_last_error(static_cast<candle_handle>(m_devHandle));
        emit errorOccurred(QString("gs_usb 发送失败: %1").arg(static_cast<int>(err)));
    } else {
        qDebug() << "gs_usb: candle_frame_send OK, channel=" << m_channelIndex;
    }
    return ret;
}

bool GsUsbAdapter::isAlive() const
{
    if (!m_opened || !m_devHandle) return false;
    if (m_deviceLost) return false;

    // 通过尝试获取设备时间戳来检测设备是否存活
    // 设备拔出时 candle_dev_get_timestamp_us 会失败
    uint32_t ts = 0;
    if (!candle_dev_get_timestamp_us(static_cast<candle_handle>(m_devHandle), &ts)) {
        candle_err_t err = candle_dev_last_error(static_cast<candle_handle>(m_devHandle));
        if (err != CANDLE_ERR_OK) {
            const_cast<GsUsbAdapter*>(this)->m_deviceLost = true;
            return false;
        }
    }
    return true;
}

QString GsUsbAdapter::channelName(int channel)
{
    int dev = (channel >> 8) & 0xFF;
    int ch = channel & 0xFF;
    return QString("candleLight #%1 CH%2").arg(dev).arg(ch);
}

