#include "gsusbadapter.h"
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QDateTime>
#include <QList>
#include <algorithm>

extern "C" {
#include <third_party/CandleApiDriver/api/candle.h>
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
            info.channel = (i << 8) | ch;
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

    // 不用 candle_channel_set_bitrate 的自动估算: 它可能选出精度差的 brp/tq 组合，
    // 位时序偏差在直连场景 (如 PCAN <-> candleLight) 下会累积并引发 Bus-Off。
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

    candle_capability_t caps;
    if (!candle_channel_get_capabilities(hdev, ch, &caps)) {
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred("gs_usb: 无法获取设备能力");
        return false;
    }

    // 按 比特率误差 > 采样点接近 80% > tq 总数 的优先级筛选候选
    struct BittimingCandidate {
        candle_bittiming_t timing;
        uint32_t tq_total;
        uint32_t actual_bitrate;
        uint32_t bitrate_err;   // |actual - target|
        uint32_t sample_point;  // x1000
    };

    QList<BittimingCandidate> candidates;
    const uint32_t ERR_THRESHOLD = bitrate / 1000; // 0.1% 偏差以内才接受

    for (uint32_t brp = caps.brp_min; brp <= caps.brp_max; brp += caps.brp_inc) {
        if (brp == 0) continue;
        double tq_total_f = static_cast<double>(caps.fclk_can) / (brp * bitrate);
        uint32_t tq_total = static_cast<uint32_t>(tq_total_f + 0.5);
        if (tq_total < 4 || tq_total > 25) continue;

        // 验证实际比特率偏差
        uint32_t actual_bitrate = caps.fclk_can / (brp * tq_total);
        uint32_t err = (actual_bitrate > bitrate)
            ? (actual_bitrate - bitrate) : (bitrate - actual_bitrate);
        if (err > ERR_THRESHOLD) continue;

        for (uint32_t tseg2 = caps.tseg2_min; tseg2 <= caps.tseg2_max && tseg2 < tq_total; tseg2++) {
            uint32_t tseg1 = tq_total - 1 - tseg2;
            if (tseg1 < caps.tseg1_min || tseg1 > caps.tseg1_max) continue;

            uint32_t sp = (1 + tseg1) * 1000 / tq_total;
            if (sp < 680 || sp > 875) continue;

            BittimingCandidate c;
            c.timing.prop_seg = 1;
            c.timing.phase_seg1 = tseg1 - 1;
            c.timing.phase_seg2 = tseg2;
            c.timing.sjw = caps.sjw_max; // 用最大 SJW 容忍时钟偏差
            c.timing.brp = brp;
            c.tq_total = tq_total;
            c.actual_bitrate = actual_bitrate;
            c.bitrate_err = err;
            c.sample_point = sp;
            candidates.append(c);
        }
    }

    if (candidates.isEmpty()) {
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 无法为 bitrate=%1 找到精确的 bittiming (fclk=%2)")
                           .arg(bitrate).arg(caps.fclk_can));
        return false;
    }

    // 优先级: 比特率误差最小 > 采样点最接近 80% > tq 总数更大
    std::sort(candidates.begin(), candidates.end(),
        [](const BittimingCandidate &a, const BittimingCandidate &b) {
            if (a.bitrate_err != b.bitrate_err)
                return a.bitrate_err < b.bitrate_err;
            uint32_t da = (a.sample_point > 800) ? (a.sample_point - 800) : (800 - a.sample_point);
            uint32_t db = (b.sample_point > 800) ? (b.sample_point - 800) : (800 - b.sample_point);
            if (da != db) return da < db;
            return a.tq_total > b.tq_total;
        });

    const BittimingCandidate &best = candidates.first();
    candle_bittiming_t timing = best.timing;

    if (!candle_channel_set_timing(hdev, ch, &timing)) {
        candle_err_t err2 = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 设置 bittiming 失败 (err=%1)").arg(static_cast<int>(err2)));
        return false;
    }

    // 普通模式启动 (总线上至少需要另一个节点才能成功 ACK)
    if (!candle_channel_start(hdev, ch, 0)) {
        candle_err_t err = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 启动通道失败 (err=%1)").arg(static_cast<int>(err)));
        return false;
    }

    m_devHandle = hdev;
    m_devList = list;
    m_channelIndex = ch;
    m_opened = true;
    m_deviceLost = false;

    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    m_readTimer = new QTimer(this);
    connect(m_readTimer, &QTimer::timeout, this, &GsUsbAdapter::onReadTimer);
    // 2ms 而非 1ms：降低 USB 端点拥塞风险
    m_readTimer->start(2);

    m_errorFrameCount = 0;
    m_recovering = false;
    m_recoverAttempt = 0;

    return true;
}

void GsUsbAdapter::close()
{
    if (m_readTimer)
        m_readTimer->stop();

    m_recovering = false;
    m_errorFrameCount = 0;
    m_recoverAttempt = 0;

    if (m_devHandle) {
        candle_channel_stop(static_cast<candle_handle>(m_devHandle), m_channelIndex);
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
    // candle/gs_usb 的 can_dlc 是 DLC 编码，而 CanMessage::dlc 是字节数
    frame.can_dlc = canFdLenToDlc(msg.dlc);
    for (int i = 0; i < msg.dlc && i < 64; ++i)
        frame.data[i] = msg.data[i];

    bool ret = candle_frame_send(static_cast<candle_handle>(m_devHandle),
                                 m_channelIndex, &frame);
    if (!ret) {
        candle_err_t err = candle_dev_last_error(static_cast<candle_handle>(m_devHandle));
        emit errorOccurred(QString("gs_usb 发送失败: %1").arg(static_cast<int>(err)));
    }
    return ret;
}

// 接收轮询 & 错误恢复

void GsUsbAdapter::onReadTimer()
{
    if (!m_opened || !m_devHandle || m_recovering)
        return;

    candle_frame_t frame;
    bool gotAnyFrame = false;

    while (candle_frame_read(static_cast<candle_handle>(m_devHandle), &frame, 0)) {
        gotAnyFrame = true;
        candle_frametype_t ftype = candle_frame_type(&frame);

        if (ftype == CANDLE_FRAMETYPE_RECEIVE) {
            m_errorFrameCount = 0;

            CanMessage msg;
            msg.id = candle_frame_id(&frame);
            // DLC 编码 > 8 即为 CAN FD 帧
            uint8_t rawDlc = candle_frame_dlc(&frame);
            msg.isFd = (rawDlc > 8);
            msg.dlc = static_cast<uint8_t>(canFdDlcToLen(rawDlc));
            msg.direction = CanDirection::Rx;
            msg.channel = m_channelIndex;
            msg.timestamp = QDateTime::currentDateTime();
            msg.type = candle_frame_is_extended_id(&frame)
                ? CanFrameType::ExtendedData : CanFrameType::StandardData;

            if (candle_frame_is_rtr(&frame))
                msg.type = CanFrameType::Remote;

            uint8_t *data = candle_frame_data(&frame);
            for (int i = 0; i < msg.dlc && i < 64; ++i)
                msg.data[i] = data[i];

            emit messageReceived(msg);
        } else if (ftype == CANDLE_FRAMETYPE_ERROR) {
            m_errorFrameCount++;
            if (m_errorFrameCount >= m_maxErrorBeforeRecover) {
                qWarning() << "gs_usb:" << m_errorFrameCount
                           << "consecutive error frames, attempting channel recovery...";
                recoverChannel();
            }
        }
        // CANDLE_FRAMETYPE_TIMESTAMP_OVFL / ECHO / UNKNOWN 忽略
    }

    // 长时间收不到帧且错误计数未清零，通道可能已静默停止
    if (!gotAnyFrame && m_errorFrameCount >= m_maxErrorBeforeRecover) {
        qWarning() << "gs_usb: no frames received with" << m_errorFrameCount
                   << "pending errors, attempting channel recovery...";
        recoverChannel();
    }
}

void GsUsbAdapter::recoverChannel()
{
    if (m_recovering || !m_devHandle || !m_opened)
        return;

    m_recoverAttempt++;
    if (m_recoverAttempt > m_maxRecoverAttempts) {
        qWarning() << "gs_usb: recovery failed after" << m_maxRecoverAttempts
                   << "attempts, marking device as lost";
        m_deviceLost = true;
        emit errorOccurred("gs_usb: 通道恢复失败，设备可能已进入不可恢复状态");
        return;
    }

    m_recovering = true;

    candle_handle dev = static_cast<candle_handle>(m_devHandle);
    uint8_t ch = m_channelIndex;
    const int attempt = m_recoverAttempt;

    if (!candle_channel_stop(dev, ch)) {
        candle_err_t err = candle_dev_last_error(dev);
        qWarning() << "gs_usb: candle_channel_stop failed, err=" << static_cast<int>(err);
    }

    // 给固件留出处理时间
    QThread::msleep(10);

    if (!candle_channel_start(dev, ch, 0)) {
        candle_err_t err = candle_dev_last_error(dev);
        qWarning() << "gs_usb: candle_channel_start failed, err=" << static_cast<int>(err);
        m_recovering = false;
        QTimer::singleShot(100, this, &GsUsbAdapter::recoverChannel);
        return;
    }

    m_errorFrameCount = 0;
    m_recovering = false;
    m_recoverAttempt = 0; // 恢复成功后复位，下次出错仍有完整重试额度

    emit errorOccurred(QString("gs_usb: 通道已自动恢复 (第 %1 次)").arg(attempt));
}

bool GsUsbAdapter::isAlive() const
{
    if (!m_opened || !m_devHandle) return false;
    if (m_deviceLost) return false;

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
    return QString("candleLight #%1").arg(dev);
}

QList<int> GsUsbAdapter::availableSendChannels() const
{
    QList<int> channels;
    if (m_opened)
        channels.append(m_channelIndex);
    return channels;
}

