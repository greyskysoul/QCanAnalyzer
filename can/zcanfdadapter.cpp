#include "zcanfdadapter.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <cstring>

int ZcanFdAdapter::s_openCount = 0;
QSet<UINT> ZcanFdAdapter::s_openDeviceIndices = {};
QList<CanDeviceInfo> ZcanFdAdapter::s_cachedDevices = {};

// ══════════════════════════════════════════════════════════════�?
// 构�?/ 析构
// ══════════════════════════════════════════════════════════════�?

ZcanFdAdapter::ZcanFdAdapter(QObject *parent)
    : CanInterface(parent)
{
}

ZcanFdAdapter::~ZcanFdAdapter()
{
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    close();
}

// ══════════════════════════════════════════════════════════════�?
// 扫描设备
// ══════════════════════════════════════════════════════════════�?

QList<CanDeviceInfo> ZcanFdAdapter::scanDevices()
{
    if (s_openCount > 0) {
        // 过滤掉已被其他会话占用的设备, 防止重复打开
        QList<CanDeviceInfo> filtered;
        for (const auto &dev : s_cachedDevices) {
            if (!s_openDeviceIndices.contains(static_cast<UINT>(dev.deviceIndex)))
                filtered.append(dev);
        }
        return filtered;
    }

    QList<CanDeviceInfo> devices;

    for (UINT devIdx = 0; devIdx < USBCANFD_MAX_NUM; ++devIdx) {
        DEVICE_HANDLE dh = ZCAN_OpenDevice(USBCANFD_200U, devIdx, 0);
        if (!dh || dh == INVALID_DEVICE_HANDLE)
            continue;

        ZCAN_DEVICE_INFO info;
        memset(&info, 0, sizeof(info));
        UINT ret = ZCAN_GetDeviceInf(dh, &info);
        if (ret == STATUS_OK) {
            info.str_Serial_Num[sizeof(info.str_Serial_Num)-1] = '\0';
            info.str_hw_Type[sizeof(info.str_hw_Type)-1] = '\0';
            int cc = (info.can_Num > 0 && info.can_Num <= 8) ? info.can_Num : 2;
            CanDeviceInfo di;
            di.channel = static_cast<int>(devIdx << 8);
            di.adapterType = static_cast<int>(CanAdapterType::ZCANFD);
            di.deviceType = USBCANFD_200U;
            di.deviceIndex = static_cast<int>(devIdx);
            di.channelCount = cc;
            di.name = QString("ZCANFD #%1").arg(devIdx);
            di.description = QString("%1 SN:%2")
                .arg((const char*)info.str_hw_Type)
                .arg((const char*)info.str_Serial_Num);
            devices.append(di);
        }
        ZCAN_CloseDevice(dh);
    }

    s_cachedDevices = devices;
    return devices;
}

// ══════════════════════════════════════════════════════════════�?
// 打开 / 关闭
// ══════════════════════════════════════════════════════════════�?

UINT ZcanFdAdapter::baudToHz(CanBaudRate baud) const
{
    switch (baud) {
    case CanBaudRate::BR_1M:   return 1000000;
    case CanBaudRate::BR_800K: return 800000;
    case CanBaudRate::BR_500K: return 500000;
    case CanBaudRate::BR_250K: return 250000;
    case CanBaudRate::BR_125K: return 125000;
    case CanBaudRate::BR_100K: return 100000;
    case CanBaudRate::BR_50K:  return 50000;
    case CanBaudRate::BR_20K:  return 20000;
    case CanBaudRate::BR_10K:  return 10000;
    case CanBaudRate::BR_5K:   return 5000;
    default: return 500000;
    }
}

bool ZcanFdAdapter::open(int channel, CanBaudRate baud)
{
    if (m_opened) close();

    UINT devIdx = (channel >> 8) & 0xFF;
    UINT chIdx  = channel & 0xFF;

    // ── 重复打开检测: 同一设备索引只能被一个实例打开 ──
    if (s_openDeviceIndices.contains(devIdx)) {
        emit errorOccurred(QString("ZCANFD: 设备 #%1 已被打开, 不能重复打开").arg(devIdx));
        return false;
    }

    // ── 1. 打开设备 ──
    m_devHandle = ZCAN_OpenDevice(m_deviceType, devIdx, 0);

    if (!m_devHandle || m_devHandle == INVALID_DEVICE_HANDLE) {
        emit errorOccurred(QString("ZCANFD: 打开设备 #%1 失败").arg(devIdx));
        return false;
    }

    m_deviceIndex = devIdx;

    // ── 获取通道信息 ──
    ZCAN_DEVICE_INFO info;
    memset(&info, 0, sizeof(info));
    UINT infoRet = ZCAN_GetDeviceInf(m_devHandle, &info);
    if (infoRet == STATUS_OK && info.can_Num > 0 && info.can_Num <= 8)
        m_totalChannels = info.can_Num;
    else
        m_totalChannels = 2;

    // ── 2. 设置 CANFD 标准 (ISO) ──
    UINT baudHz = baudToHz(baud);
    UINT dataBaudHz = 2000000;  // 数据域默认 2MHz
    for (int ch = 0; ch < m_totalChannels; ++ch) {
        ZCAN_SetCANFDStandard(m_devHandle, static_cast<UINT>(ch), 0);  // 0=ISO
        ZCAN_SetAbitBaud(m_devHandle, static_cast<UINT>(ch), baudHz);
        ZCAN_SetDbitBaud(m_devHandle, static_cast<UINT>(ch), dataBaudHz);
    }

    // ── 3. 初始化所有通道 (can_type=TYPE_CANFD, 波特率由上面 API 设定) ──
    m_openChannels.clear();
    for (int ch = 0; ch < m_totalChannels; ++ch) {
        ZCAN_CHANNEL_INIT_CONFIG initConfig;
        memset(&initConfig, 0, sizeof(initConfig));
        initConfig.can_type = TYPE_CANFD;  // 必须用 CANFD 模式, 参照官方例程
        initConfig.canfd.mode = 0;         // 0=正常模式

        CHANNEL_HANDLE chHandle = ZCAN_InitCAN(m_devHandle, static_cast<UINT>(ch), &initConfig);

        if (!chHandle || chHandle == INVALID_CHANNEL_HANDLE) {
            qWarning() << "[ZCANFD] InitCAN ch" << ch << "failed";
            if (ch == static_cast<int>(chIdx)) {
                emit errorOccurred(QString("ZCANFD: 初始化 CAN 通道 %1 失败").arg(ch));
                close();
                return false;
            }
            continue;
        }

        // ── 4. 使能终端电阻 ──
        ZCAN_SetResistanceEnable(m_devHandle, static_cast<UINT>(ch), 1);

        // ── 5. 启动 CAN ──
        UINT ret = ZCAN_StartCAN(chHandle);
        if (ret != STATUS_OK) {
            qWarning() << "[ZCANFD] StartCAN ch" << ch << "failed, ret=" << Qt::hex << ret;
            if (ch == static_cast<int>(chIdx)) {
                emit errorOccurred(QString("ZCANFD: 启动 CAN 通道 %1 失败").arg(ch));
                close();
                return false;
            }
            continue;
        }

        // ── 6. 清空缓冲区 ──
        ZCAN_ClearBuffer(chHandle);

        ChannelInfo ci;
        ci.handle = chHandle;
        ci.chIdx = static_cast<UINT>(ch);
        ci.baud = baud;
        m_openChannels.append(ci);
    }

    if (m_openChannels.isEmpty()) { close(); return false; }

    m_canIndex = chIdx;
    { bool f = false;
      for (auto &c : m_openChannels) if (c.chIdx == chIdx) { f = true; break; }
      if (!f) m_canIndex = m_openChannels.first().chIdx; }

    m_opened = true;
    ++s_openCount;
    s_openDeviceIndices.insert(m_deviceIndex);

    if (!m_readTimer) {
        m_readTimer = new QTimer(this);
        connect(m_readTimer, &QTimer::timeout, this, &ZcanFdAdapter::onReadTimer);
    }
    m_readTimer->start(1);

    return true;
}

void ZcanFdAdapter::close()
{
    if (!m_opened) return;

    if (m_readTimer) {
        m_readTimer->stop();
    }

    for (auto &ci : m_openChannels) {
        if (ci.handle)
            ZCAN_ResetCAN(ci.handle);
    }
    m_openChannels.clear();

    if (m_devHandle) {
        ZCAN_CloseDevice(m_devHandle);
        m_devHandle = nullptr;
    }

    m_opened = false;
    s_openDeviceIndices.remove(m_deviceIndex);
    if (s_openCount > 0) --s_openCount;
}

bool ZcanFdAdapter::isOpen() const
{
    return m_opened;
}

bool ZcanFdAdapter::isAlive() const
{
    if (!m_opened || !m_devHandle) return false;
    return m_opened;
}

// ══════════════════════════════════════════════════════════════�?
// 读取轮询
// ══════════════════════════════════════════════════════════════�?

void ZcanFdAdapter::onReadTimer()
{
    if (!m_opened) return;
    pollMessages();
}

void ZcanFdAdapter::pollMessages()
{
    for (const auto &ci : m_openChannels) {
        CHANNEL_HANDLE handle = ci.handle;
        if (!handle) continue;
    ZCAN_ReceiveFD_Data fdData[16];
    memset(fdData, 0, sizeof(fdData));

    UINT fdCount = ZCAN_ReceiveFD(handle, fdData, 16, 0);

    for (UINT i = 0; i < fdCount; ++i) {
        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = static_cast<int>(ci.chIdx);
        msg.timestamp = QDateTime::currentDateTime();

        UINT canId = fdData[i].frame.can_id;
        if (IS_EFF(canId)) {
            msg.type = CanFrameType::ExtendedData;
            msg.id = GET_ID(canId);
        } else {
            msg.type = CanFrameType::StandardData;
            msg.id = GET_ID(canId);
        }
        if (IS_RTR(canId))
            msg.type = CanFrameType::Remote;
        if (IS_ERR(canId))
            msg.type = CanFrameType::Error;

        msg.dlc = fdData[i].frame.len;
        msg.isFd = true;
        int dataLen = canFdDlcToLen(fdData[i].frame.len);
        dataLen = qMin(dataLen, CANFD_MAX_DLEN);
        for (int j = 0; j < dataLen; ++j)
            msg.data[j] = fdData[i].frame.data[j];

        emit messageReceived(msg);
    }

    // ── 尝试标准 CAN 接收 ──
    ZCAN_Receive_Data canData[16];
    memset(canData, 0, sizeof(canData));

    UINT canCount = ZCAN_Receive(handle, canData, 16, 0);

    for (UINT i = 0; i < canCount; ++i) {
        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = static_cast<int>(ci.chIdx);
        msg.timestamp = QDateTime::currentDateTime();

        UINT canId = canData[i].frame.can_id;
        if (IS_EFF(canId)) {
            msg.type = CanFrameType::ExtendedData;
            msg.id = GET_ID(canId);
        } else {
            msg.type = CanFrameType::StandardData;
            msg.id = GET_ID(canId);
        }
        if (IS_RTR(canId))
            msg.type = CanFrameType::Remote;
        if (IS_ERR(canId))
            msg.type = CanFrameType::Error;

        msg.dlc = canData[i].frame.can_dlc;
        msg.isFd = false;
        int dataLen = msg.dlc > 8 ? 8 : msg.dlc;
        for (int j = 0; j < dataLen && j < CAN_MAX_DLEN; ++j)
            msg.data[j] = canData[i].frame.data[j];

        emit messageReceived(msg);
    }
    }  // close for (const auto &ci : m_openChannels)
}  // close pollMessages()

// ══════════════════════════════════════════════════════════════�?
// 发送
// ══════════════════════════════════════════════════════════════�?

bool ZcanFdAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened || m_openChannels.isEmpty()) return false;

    CHANNEL_HANDLE targetHandle = nullptr;
    for (const auto &ci : m_openChannels) {
        if (static_cast<int>(ci.chIdx) == msg.channel || ci.chIdx == m_canIndex) {
            targetHandle = ci.handle;
            break;
        }
    }
    if (!targetHandle) targetHandle = m_openChannels.first().handle;

    if (msg.isFd && msg.dlc > 8) {
        // ── CAN FD 发�?──
        ZCAN_TransmitFD_Data fdData;
        memset(&fdData, 0, sizeof(fdData));
        fdData.transmit_type = 0; // 正常发�?

        UINT flags = 0;
        if (msg.type == CanFrameType::ExtendedData)
            flags |= CAN_EFF_FLAG;
        if (msg.type == CanFrameType::Remote)
            flags |= CAN_RTR_FLAG;
        fdData.frame.can_id = msg.id | flags;
        fdData.frame.len = msg.dlc;
        int dataLen = qMin((int)msg.dlc, CANFD_MAX_DLEN);
        for (int j = 0; j < dataLen; ++j)
            fdData.frame.data[j] = msg.data[j];

        UINT ret;
        ret = ZCAN_TransmitFD(targetHandle, &fdData, 1);
        if (ret != STATUS_OK) {
            emit errorOccurred(QString("ZCANFD: CAN FD 发送失�?(0x%1)").arg(ret, 4, 16, QChar('0')));
            return false;
        }
    } else {
        // ── 标准 CAN 发�?──
        ZCAN_Transmit_Data canData;
        memset(&canData, 0, sizeof(canData));
        canData.transmit_type = 0;

        UINT flags = 0;
        if (msg.type == CanFrameType::ExtendedData)
            flags |= CAN_EFF_FLAG;
        if (msg.type == CanFrameType::Remote)
            flags |= CAN_RTR_FLAG;
        canData.frame.can_id = msg.id | flags;
        canData.frame.can_dlc = msg.dlc > 8 ? 8 : msg.dlc;
        int dataLen = canData.frame.can_dlc;
        for (int j = 0; j < dataLen && j < CAN_MAX_DLEN; ++j)
            canData.frame.data[j] = msg.data[j];

        UINT ret;
        ret = ZCAN_Transmit(targetHandle, &canData, 1);
        if (ret != STATUS_OK) {
            emit errorOccurred(QString("ZCANFD: 发送失�?(0x%1)").arg(ret, 4, 16, QChar('0')));
            return false;
        }
    }

    return true;
}

// ══════════════════════════════════════════════════════════════�?
// 多通道支持
// ══════════════════════════════════════════════════════════════�?

QList<int> ZcanFdAdapter::availableSendChannels() const
{
    QList<int> chs;
    int n = m_totalChannels > 0 ? m_totalChannels : 1;
    for (int i = 0; i < n; ++i) chs.append(i);
    return chs;
}

bool ZcanFdAdapter::setSendChannel(int ch)
{
    if (ch < 0 || ch >= m_totalChannels) return false;
    for (const auto &ci : m_openChannels)
        if (static_cast<int>(ci.chIdx) == ch) { m_canIndex = static_cast<UINT>(ch); return true; }
    return false;
}

int ZcanFdAdapter::currentSendChannel() const { return static_cast<int>(m_canIndex); }

// ══════════════════════════════════════════════════════════════�?
// 辅助
// ══════════════════════════════════════════════════════════════�?

QString ZcanFdAdapter::channelName(int channel)
{
    UINT devIdx = (channel >> 8) & 0xFF;
    return QString("ZCANFD #%1").arg(devIdx);
}

QString ZcanFdAdapter::errorText(UINT err)
{
    switch (err) {
    case STATUS_OK: return "OK";
    case STATUS_ERR: return "Error";
    case STATUS_ONLINE: return "Online";
    case STATUS_OFFLINE: return "Offline";
    case STATUS_UNSUPPORTED: return "Unsupported";
    default: return QString("0x%1").arg(err, 8, 16, QChar('0'));
    }
}

