#include "gsusbadapter.h"
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QDateTime>
#include <QList>
#include <algorithm>

// candle API 头文件
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

    // ─── 波特率设置 ──────────────────────────────────────────
    // 始终使用精确 bittiming 计算，而非 candle_channel_set_bitrate 的自动估算。
    // 自动估算可能选择精度差的 brp/tq 组合，导致位时序偏差累积，
    // 在直连场景（如 PCAN<->candleLight）中引发 Bus-Off。
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

    // 获取设备 CAN 时钟能力
    candle_capability_t caps;
    if (!candle_channel_get_capabilities(hdev, ch, &caps)) {
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred("gs_usb: 无法获取设备能力");
        return false;
    }

    // 两阶段搜索：先找精度最优（实际比特率偏差最小），再从中选采样点最佳
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
        // 用浮点计算 tq_total 避免截断误差累积
        double tq_total_f = static_cast<double>(caps.fclk_can) / (brp * bitrate);
        uint32_t tq_total = static_cast<uint32_t>(tq_total_f + 0.5); // 四舍五入
        if (tq_total < 4 || tq_total > 25) continue;

        // 验证实际比特率偏差
        uint32_t actual_bitrate = caps.fclk_can / (brp * tq_total);
        uint32_t err = (actual_bitrate > bitrate)
            ? (actual_bitrate - bitrate) : (bitrate - actual_bitrate);
        if (err > ERR_THRESHOLD) continue;

        for (uint32_t tseg2 = caps.tseg2_min; tseg2 <= caps.tseg2_max && tseg2 < tq_total; tseg2++) {
            uint32_t tseg1 = tq_total - 1 - tseg2;
            if (tseg1 < caps.tseg1_min || tseg1 > caps.tseg1_max) continue;

            // 采样点 = (1 + tseg1) / tq_total, 放宽范围到 68%~87.5%
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

    // 排序策略：优先级 = 比特率误差最低 > 采样点最接近 80% > tq_total 更大（更精细）
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
    // 普通模式启动 CAN 通道（需要总线至少两个节点才能正常 ACK）
    uint32_t flags = 0;
    if (!candle_channel_start(hdev, ch, flags)) {
        candle_err_t err = candle_dev_last_error(hdev);
        candle_dev_free(hdev);
        candle_list_free(list);
        emit errorOccurred(QString("gs_usb: 启动通道失败 (err=%1)").arg(static_cast<int>(err)));
        return false;
    }

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
    connect(m_readTimer, &QTimer::timeout, this, &GsUsbAdapter::onReadTimer);
    // 轮询间隔从 1ms 放宽至 2ms，降低 USB 端点拥塞风险
    m_readTimer->start(2);

    m_errorFrameCount = 0;
    m_recovering = false;
    m_recoverAttempt = 0;

    return true;
}

void GsUsbAdapter::close()
{
    // 先停止读取定时器
    if (m_readTimer) {
        m_readTimer->stop();
    }

    m_recovering = false;
    m_errorFrameCount = 0;
    m_recoverAttempt = 0;

    if (m_devHandle) {
        // 先停止通道再关闭设备
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
    frame.can_dlc = msg.dlc;
    int copyLen = msg.isFd ? qMin((int)msg.dlc, 64) : qMin((int)msg.dlc, 8);
    for (int i = 0; i < copyLen; ++i)
        frame.data[i] = msg.data[i];

    bool ret = candle_frame_send(static_cast<candle_handle>(m_devHandle),
                                 m_channelIndex, &frame);
    if (!ret) {
        candle_err_t err = candle_dev_last_error(static_cast<candle_handle>(m_devHandle));
        emit errorOccurred(QString("gs_usb 发送失败: %1").arg(static_cast<int>(err)));
    }
    return ret;
}

// ═══════════════════════════════════════════════════════════════
// 接收轮询 & 错误恢复
// ═══════════════════════════════════════════════════════════════

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
            // 收到正常数据帧 -> 重置错误计数器
            if (m_errorFrameCount > 0) {
                m_errorFrameCount = 0;
            }

            CanMessage msg;
            msg.id = candle_frame_id(&frame);
            uint8_t rawDlc = candle_frame_dlc(&frame);
            msg.dlc = rawDlc;
            msg.isFd = (rawDlc > 8); // gs_usb: DLC > 8 视为 FD 帧
            msg.direction = CanDirection::Rx;
            msg.channel = m_channelIndex;
            msg.timestamp = QDateTime::currentDateTime();
            msg.type = candle_frame_is_extended_id(&frame)
                ? CanFrameType::ExtendedData : CanFrameType::StandardData;

            // 检测 RTR
            if (candle_frame_is_rtr(&frame))
                msg.type = CanFrameType::Remote;

            int dataLen = msg.isFd ? canFdDlcToLen(rawDlc) : (rawDlc > 8 ? 8 : rawDlc);
            uint8_t *data = candle_frame_data(&frame);
            for (int i = 0; i < dataLen && i < 64; ++i)
                msg.data[i] = data[i];

            emit messageReceived(msg);
        } else if (ftype == CANDLE_FRAMETYPE_ERROR) {
            // 错误帧: CAN 控制器报告总线错误
            m_errorFrameCount++;

            // 错误帧连续超过阈值 -> 可能是 Bus-Off，尝试恢复
            if (m_errorFrameCount >= m_maxErrorBeforeRecover) {
                qWarning() << "gs_usb:" << m_errorFrameCount
                           << "consecutive error frames, attempting channel recovery...";
                recoverChannel();
            }
        } else if (ftype == CANDLE_FRAMETYPE_TIMESTAMP_OVFL) {
            // 时间戳溢出，忽略
        }
        // CANDLE_FRAMETYPE_ECHO / UNKNOWN 忽略
    }

    // 如果本轮没读到任何帧（可能固件 FIFO 已空但通道仍在运行），
    // 且之前累积了大量错误帧 -> 通道可能已静默停止
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

    // Step 1: 停止通道
    if (!candle_channel_stop(dev, ch)) {
        candle_err_t err = candle_dev_last_error(dev);
        qWarning() << "gs_usb: candle_channel_stop failed, err=" << static_cast<int>(err);
    }

    // Step 2: 短暂等待固件处理
    QThread::msleep(10);

    // Step 3: 重新启动通道（使用相同标志）
    if (!candle_channel_start(dev, ch, 0)) {
        candle_err_t err = candle_dev_last_error(dev);
        qWarning() << "gs_usb: candle_channel_start failed, err=" << static_cast<int>(err);
        m_recovering = false;
        // 立即重试
        QTimer::singleShot(100, this, &GsUsbAdapter::recoverChannel);
        return;
    }

    m_errorFrameCount = 0;
    m_recovering = false;
    // 恢复成功后重置尝试计数（下次如果再出错仍有完整重试额度）
    m_recoverAttempt = 0;

    emit errorOccurred(QString("gs_usb: 通道已自动恢复 (第 %1 次)").arg(m_recoverAttempt));
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
    return QString("candleLight #%1").arg(dev);
}

