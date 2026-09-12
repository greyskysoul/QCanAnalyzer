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

namespace {

/// 按 比特率误差 > 采样点接近 80% > tq 总数 的优先级挑选最优 bittiming。
/// 不用 candle_channel_set_bitrate 的自动估算：它可能选出精度差的 brp/tq 组合，
/// 位时序偏差在直连场景（如 PCAN <-> candleLight）下会累积并引发 Bus-Off。
bool findBittiming(const candle_capability_t &caps, uint32_t bitrate,
                   candle_bittiming_t *out, QString *error)
{
    struct Candidate {
        candle_bittiming_t timing;
        uint32_t tqTotal;
        uint32_t bitrateErr;   // |actual - target|
        uint32_t samplePoint;  // x1000
    };

    const uint32_t errThreshold = bitrate / 1000; // 0.1% 偏差以内
    QList<Candidate> candidates;

    for (uint32_t brp = caps.brp_min; brp <= caps.brp_max; brp += caps.brp_inc) {
        if (brp == 0) continue;
        const uint32_t tqTotal = static_cast<uint32_t>(
            static_cast<double>(caps.fclk_can) / (brp * bitrate) + 0.5);
        if (tqTotal < 4 || tqTotal > 25) continue;

        const uint32_t actual = caps.fclk_can / (brp * tqTotal);
        const uint32_t err = (actual > bitrate) ? (actual - bitrate) : (bitrate - actual);
        if (err > errThreshold) continue;

        for (uint32_t tseg2 = caps.tseg2_min; tseg2 <= caps.tseg2_max && tseg2 < tqTotal; ++tseg2) {
            const uint32_t tseg1 = tqTotal - 1 - tseg2;
            if (tseg1 < caps.tseg1_min || tseg1 > caps.tseg1_max) continue;

            const uint32_t sp = (1 + tseg1) * 1000 / tqTotal;
            if (sp < 680 || sp > 875) continue;

            Candidate c;
            c.timing.prop_seg = 1;
            c.timing.phase_seg1 = tseg1 - 1;
            c.timing.phase_seg2 = tseg2;
            c.timing.sjw = caps.sjw_max; // 最大 SJW 容忍时钟偏差
            c.timing.brp = brp;
            c.tqTotal = tqTotal;
            c.bitrateErr = err;
            c.samplePoint = sp;
            candidates.append(c);
        }
    }

    if (candidates.isEmpty()) {
        *error = QString("无法为 %1 Hz 找到精确的 bittiming (fclk=%2)")
                     .arg(bitrate).arg(caps.fclk_can);
        return false;
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate &a, const Candidate &b) {
            if (a.bitrateErr != b.bitrateErr) return a.bitrateErr < b.bitrateErr;
            const uint32_t da = (a.samplePoint > 800) ? a.samplePoint - 800 : 800 - a.samplePoint;
            const uint32_t db = (b.samplePoint > 800) ? b.samplePoint - 800 : 800 - b.samplePoint;
            if (da != db) return da < db;
            return a.tqTotal > b.tqTotal;
        });

    *out = candidates.first().timing;
    return true;
}

} // namespace

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

bool GsUsbAdapter::open(int channel, CanBaudRate baud, CanDataBaudRate dataBaud)
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

    const bool fdEnabled = (dataBaud != CanDataBaudRate::None);

    candle_capability_t caps;
    if (!candle_channel_get_capabilities(hdev, ch, &caps)) {
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred("gs_usb: 无法获取设备能力");
        return false;
    }

    QString bittimingError;
    candle_bittiming_t timing;
    if (!findBittiming(caps, baudRateHz(baud), &timing, &bittimingError)) {
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred("gs_usb: 仲裁域 " + bittimingError);
        return false;
    }

    candle_bittiming_t dataTiming;
    if (fdEnabled) {
        if (!(caps.feature & CANDLE_FEATURE_FD)) {
            candle_dev_free(hdev);
            candle_list_free(list);
            emit errorOccurred("gs_usb: 设备不支持 CAN FD");
            return false;
        }
        if (!findBittiming(caps, dataBaudRateHz(dataBaud), &dataTiming, &bittimingError)) {
            candle_dev_free(hdev);
            candle_list_free(list);
            emit errorOccurred("gs_usb: 数据域 " + bittimingError);
            return false;
        }
    }

    if (!candle_channel_set_timing(hdev, ch, &timing)) {
        candle_err_t err = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 设置仲裁域 bittiming 失败 (err=%1)").arg(static_cast<int>(err)));
        return false;
    }

    if (fdEnabled && !candle_channel_set_data_timing(hdev, ch, &dataTiming)) {
        candle_err_t err = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 设置数据域 bittiming 失败 (err=%1)").arg(static_cast<int>(err)));
        return false;
    }

    // CAN-FD 必须以 FD 模式启动；普通模式要求总线上至少还有另一个节点才能 ACK
    if (!candle_channel_start(hdev, ch, fdEnabled ? CANDLE_MODE_FD : 0)) {
        candle_err_t err = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 启动通道失败 (err=%1)").arg(static_cast<int>(err)));
        return false;
    }

    m_devHandle = hdev;
    m_devList = list;
    m_channelIndex = ch;
    m_dataBaud = dataBaud;
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
    if (msg.isFd) {
        frame.flags |= CANDLE_FLAG_FD;
        if (m_dataBaud != CanDataBaudRate::None)
            frame.flags |= CANDLE_FLAG_BRS; // 数据段切换到数据域波特率
    }
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
            // 以 CANDLE_FLAG_FD 为准；部分固件不置位，故保留 DLC 编码 > 8 的兼容判断
            uint8_t rawDlc = candle_frame_dlc(&frame);
            msg.isFd = (frame.flags & CANDLE_FLAG_FD) != 0 || rawDlc > 8;
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

    // 用与 open() 相同的模式标志重启，否则 FD 会话会退回经典 CAN
    const uint32_t restartFlags = (m_dataBaud != CanDataBaudRate::None) ? CANDLE_MODE_FD : 0;
    if (!candle_channel_start(dev, ch, restartFlags)) {
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

