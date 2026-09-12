#include "zcanfdadapter.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <cstring>

int ZcanFdAdapter::s_openCount = 0;
QSet<UINT> ZcanFdAdapter::s_openDeviceIndices = {};
QList<CanDeviceInfo> ZcanFdAdapter::s_cachedDevices = {};

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

QList<CanDeviceInfo> ZcanFdAdapter::scanDevices()
{
    if (s_openCount > 0) {
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

bool ZcanFdAdapter::open(int channel, CanBaudRate baud, CanDataBaudRate dataBaud)
{
    if (m_opened) close();

    UINT devIdx = (channel >> 8) & 0xFF;
    UINT chIdx  = channel & 0xFF;

    if (s_openDeviceIndices.contains(devIdx)) {
        emit errorOccurred(QString("ZCANFD: 设备 #%1 已被打开, 不能重复打开").arg(devIdx));
        return false;
    }

    m_devHandle = ZCAN_OpenDevice(m_deviceType, devIdx, 0);

    if (!m_devHandle || m_devHandle == INVALID_DEVICE_HANDLE) {
        emit errorOccurred(QString("ZCANFD: 打开设备 #%1 失败").arg(devIdx));
        return false;
    }

    m_deviceIndex = devIdx;

    ZCAN_DEVICE_INFO info;
    memset(&info, 0, sizeof(info));
    UINT infoRet = ZCAN_GetDeviceInf(m_devHandle, &info);
    if (infoRet == STATUS_OK && info.can_Num > 0 && info.can_Num <= 8)
        m_totalChannels = info.can_Num;
    else
        m_totalChannels = 2;

    // 仲裁域始终按选定波特率；数据域仅在 FD 会话（dataBaud != None）中配置
    const UINT abitHz = baudRateHz(baud);
    const bool fdEnabled = (dataBaud != CanDataBaudRate::None);
    for (int ch = 0; ch < m_totalChannels; ++ch) {
        ZCAN_SetCANFDStandard(m_devHandle, static_cast<UINT>(ch), 0);  // 0=ISO
        ZCAN_SetAbitBaud(m_devHandle, static_cast<UINT>(ch), abitHz);
        if (fdEnabled)
            ZCAN_SetDbitBaud(m_devHandle, static_cast<UINT>(ch), dataBaudRateHz(dataBaud));
    }

    m_openChannels.clear();
    for (int ch = 0; ch < m_totalChannels; ++ch) {
        ZCAN_CHANNEL_INIT_CONFIG initConfig;
        memset(&initConfig, 0, sizeof(initConfig));
        initConfig.can_type = TYPE_CANFD;
        initConfig.canfd.mode = 0;  // 0=正常模式

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

        ZCAN_SetResistanceEnable(m_devHandle, static_cast<UINT>(ch), 1);

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

        ZCAN_ClearBuffer(chHandle);

        ChannelInfo ci;
        ci.handle = chHandle;
        ci.chIdx = static_cast<UINT>(ch);
        m_openChannels.append(ci);
    }

    if (m_openChannels.isEmpty()) { close(); return false; }

    // 若请求的通道未打开，回退到第一个可用通道
    m_canIndex = chIdx;
    bool chFound = false;
    for (const auto &c : m_openChannels) {
        if (c.chIdx == chIdx) { chFound = true; break; }
    }
    if (!chFound) m_canIndex = m_openChannels.first().chIdx;

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
    return m_opened && m_devHandle;
}

void ZcanFdAdapter::onReadTimer()
{
    if (m_opened)
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
            msg.isFd = true;

            UINT canId = fdData[i].frame.can_id;
            if (IS_EFF(canId))
                msg.type = CanFrameType::ExtendedData;
            else
                msg.type = CanFrameType::StandardData;
            if (IS_RTR(canId))
                msg.type = CanFrameType::Remote;
            if (IS_ERR(canId))
                msg.type = CanFrameType::Error;
            msg.id = GET_ID(canId);

            // ZCAN 的 len 已是字节数（非 DLC 编码）
            msg.dlc = static_cast<uint8_t>(qMin<int>(fdData[i].frame.len, CANFD_MAX_DLEN));
            for (int j = 0; j < msg.dlc; ++j)
                msg.data[j] = fdData[i].frame.data[j];

            emit messageReceived(msg);
        }

        // 同一通道也可能收到经典 CAN 帧
        ZCAN_Receive_Data canData[16];
        memset(canData, 0, sizeof(canData));
        UINT canCount = ZCAN_Receive(handle, canData, 16, 0);

        for (UINT i = 0; i < canCount; ++i) {
            CanMessage msg;
            msg.direction = CanDirection::Rx;
            msg.channel = static_cast<int>(ci.chIdx);
            msg.timestamp = QDateTime::currentDateTime();
            msg.isFd = false;

            UINT canId = canData[i].frame.can_id;
            if (IS_EFF(canId))
                msg.type = CanFrameType::ExtendedData;
            else
                msg.type = CanFrameType::StandardData;
            if (IS_RTR(canId))
                msg.type = CanFrameType::Remote;
            if (IS_ERR(canId))
                msg.type = CanFrameType::Error;
            msg.id = GET_ID(canId);

            msg.dlc = canData[i].frame.can_dlc > 8 ? 8 : canData[i].frame.can_dlc;
            for (int j = 0; j < msg.dlc && j < CAN_MAX_DLEN; ++j)
                msg.data[j] = canData[i].frame.data[j];

            emit messageReceived(msg);
        }
    }
}

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

    UINT flags = 0;
    if (msg.type == CanFrameType::ExtendedData)
        flags |= CAN_EFF_FLAG;
    if (msg.type == CanFrameType::Remote)
        flags |= CAN_RTR_FLAG;

    UINT ret;
    if (msg.isFd && msg.dlc > 8) {
        const int dataLen = canFdSnapLen(msg.dlc);
        ZCAN_TransmitFD_Data fdData;
        memset(&fdData, 0, sizeof(fdData));
        fdData.transmit_type = 0;
        fdData.frame.can_id = msg.id | flags;
        fdData.frame.len = static_cast<BYTE>(dataLen);
        for (int j = 0; j < dataLen; ++j)
            fdData.frame.data[j] = msg.data[j];
        ret = ZCAN_TransmitFD(targetHandle, &fdData, 1);
    } else {
        ZCAN_Transmit_Data canData;
        memset(&canData, 0, sizeof(canData));
        canData.transmit_type = 0;
        canData.frame.can_id = msg.id | flags;
        canData.frame.can_dlc = msg.dlc > 8 ? 8 : msg.dlc;
        for (int j = 0; j < canData.frame.can_dlc; ++j)
            canData.frame.data[j] = msg.data[j];
        ret = ZCAN_Transmit(targetHandle, &canData, 1);
    }

    if (ret != STATUS_OK) {
        emit errorOccurred(QString("ZCANFD: 发送失败 (0x%1)").arg(ret, 4, 16, QChar('0')));
        return false;
    }
    return true;
}

QList<int> ZcanFdAdapter::availableSendChannels() const
{
    QList<int> chs;
    for (int i = 0; i < m_totalChannels; ++i)
        chs.append(i);
    return chs;
}

bool ZcanFdAdapter::setSendChannel(int ch)
{
    if (ch < 0 || ch >= m_totalChannels) return false;
    for (const auto &ci : m_openChannels) {
        if (static_cast<int>(ci.chIdx) == ch) {
            m_canIndex = static_cast<UINT>(ch);
            return true;
        }
    }
    return false;
}

int ZcanFdAdapter::currentSendChannel() const
{
    return static_cast<int>(m_canIndex);
}

QString ZcanFdAdapter::channelName(int channel)
{
    UINT devIdx = (channel >> 8) & 0xFF;
    return QString("ZCANFD #%1").arg(devIdx);
}

