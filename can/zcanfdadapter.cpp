#include "zcanfdadapter.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════

ZcanFdAdapter::ZcanFdAdapter(QObject *parent)
    : CanInterface(parent)
{
#ifdef Q_OS_WIN
    loadLibrary();
#endif
}

ZcanFdAdapter::~ZcanFdAdapter()
{
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    close();
#ifdef Q_OS_WIN
    if (m_loaded) {
        unloadLibrary();
    } else if (m_library) {
        delete m_library;
        m_library = nullptr;
    }
#endif
}

// ═══════════════════════════════════════════════════════════════
// Windows: 动态加载 ControlCANFD.dll
// ═══════════════════════════════════════════════════════════════

#ifdef Q_OS_WIN

bool ZcanFdAdapter::loadLibrary()
{
    if (m_loaded) return true;

    // 尝试从 exe 同目录或 third_party/zcanfd/ 加载
    m_library = new QLibrary("ControlCANFD");
    if (!m_library->load()) {
        // 尝试带路径加载
        m_library->setFileName("third_party/zcanfd/ControlCANFD.dll");
        if (!m_library->load()) {
            qWarning() << "ControlCANFD.dll 加载失败 (无ZCANFD驱动):" << m_library->errorString();
            delete m_library;
            m_library = nullptr;
            return false;
        }
    }

    m_openDevice       = (ZCAN_OpenDevice_t)       m_library->resolve("ZCAN_OpenDevice");
    m_closeDevice      = (ZCAN_CloseDevice_t)      m_library->resolve("ZCAN_CloseDevice");
    m_getDeviceInf     = (ZCAN_GetDeviceInf_t)     m_library->resolve("ZCAN_GetDeviceInf");
    m_initCAN          = (ZCAN_InitCAN_t)          m_library->resolve("ZCAN_InitCAN");
    m_startCAN         = (ZCAN_StartCAN_t)         m_library->resolve("ZCAN_StartCAN");
    m_resetCAN         = (ZCAN_ResetCAN_t)         m_library->resolve("ZCAN_ResetCAN");
    m_clearBuffer      = (ZCAN_ClearBuffer_t)      m_library->resolve("ZCAN_ClearBuffer");
    m_getReceiveNum    = (ZCAN_GetReceiveNum_t)    m_library->resolve("ZCAN_GetReceiveNum");
    m_transmit         = (ZCAN_Transmit_t)         m_library->resolve("ZCAN_Transmit");
    m_receive          = (ZCAN_Receive_t)          m_library->resolve("ZCAN_Receive");
    m_transmitFD       = (ZCAN_TransmitFD_t)       m_library->resolve("ZCAN_TransmitFD");
    m_receiveFD        = (ZCAN_ReceiveFD_t)        m_library->resolve("ZCAN_ReceiveFD");
    m_setBaudRateCustom= (ZCAN_SetBaudRateCustom_t) m_library->resolve("ZCAN_SetBaudRateCustom");

    if (!m_openDevice || !m_closeDevice || !m_initCAN || !m_startCAN
        || !m_transmit || !m_receive) {
        qWarning() << "ControlCANFD.dll 函数解析失败";
        unloadLibrary();
        return false;
    }

    m_loaded = true;
    return true;
}

void ZcanFdAdapter::unloadLibrary()
{
    if (m_library) {
        m_library->unload();
        delete m_library;
        m_library = nullptr;
    }
    m_openDevice = nullptr;
    m_closeDevice = nullptr;
    m_getDeviceInf = nullptr;
    m_initCAN = nullptr;
    m_startCAN = nullptr;
    m_resetCAN = nullptr;
    m_clearBuffer = nullptr;
    m_getReceiveNum = nullptr;
    m_transmit = nullptr;
    m_receive = nullptr;
    m_transmitFD = nullptr;
    m_receiveFD = nullptr;
    m_setBaudRateCustom = nullptr;
    m_loaded = false;
}

#endif // Q_OS_WIN

// ═══════════════════════════════════════════════════════════════
// 扫描设备
// ═══════════════════════════════════════════════════════════════

QList<CanDeviceInfo> ZcanFdAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;

#ifndef Q_OS_WIN
    // Linux: 静态链接, 直接调用 API
    for (UINT devIdx = 0; devIdx < USBCANFD_MAX_NUM; ++devIdx) {
        DEVICE_HANDLE dh = ZCAN_OpenDevice(USBCANFD_200U, devIdx, 0);
        if (!dh || dh == INVALID_DEVICE_HANDLE)
            continue;

        ZCAN_DEVICE_INFO info;
        memset(&info, 0, sizeof(info));
        UINT ret = ZCAN_GetDeviceInf(dh, &info);
        if (ret == STATUS_OK) {
            info.str_Serial_Num[sizeof(info.str_Serial_Num) - 1] = '\0';
            info.str_hw_Type[sizeof(info.str_hw_Type) - 1] = '\0';

            for (UINT ch = 0; ch < info.can_Num; ++ch) {
                CanDeviceInfo devInfo;
                // 编码: 高字节=设备索引, 低字节=通道号
                devInfo.channel = (devIdx << 8) | ch;
                devInfo.adapterType = static_cast<int>(CanAdapterType::ZCANFD);
                devInfo.name = QString("ZCANFD #%1 CH%2").arg(devIdx).arg(ch);
                devInfo.description = QString("%1 SN:%2 [CH%3]")
                    .arg((const char*)info.str_hw_Type)
                    .arg((const char*)info.str_Serial_Num)
                    .arg(ch);
                devices.append(devInfo);
            }
        }
        ZCAN_CloseDevice(dh);
    }
#else
    // Windows: 使用动态加载的函数指针
    if (!m_loaded) return devices;

    for (UINT devIdx = 0; devIdx < USBCANFD_MAX_NUM; ++devIdx) {
        DEVICE_HANDLE dh = m_openDevice(USBCANFD_200U, devIdx, 0);
        if (!dh || dh == INVALID_DEVICE_HANDLE)
            continue;

        ZCAN_DEVICE_INFO info;
        memset(&info, 0, sizeof(info));
        if (m_getDeviceInf) {
            UINT ret = m_getDeviceInf(dh, &info);
            if (ret == STATUS_OK) {
                info.str_Serial_Num[sizeof(info.str_Serial_Num) - 1] = '\0';
                info.str_hw_Type[sizeof(info.str_hw_Type) - 1] = '\0';

                for (UINT ch = 0; ch < info.can_Num; ++ch) {
                    CanDeviceInfo devInfo;
                    devInfo.channel = (devIdx << 8) | ch;
                    devInfo.adapterType = static_cast<int>(CanAdapterType::ZCANFD);
                    devInfo.name = QString("ZCANFD #%1 CH%2").arg(devIdx).arg(ch);
                    devInfo.description = QString("%1 SN:%2 [CH%3]")
                        .arg((const char*)info.str_hw_Type)
                        .arg((const char*)info.str_Serial_Num)
                        .arg(ch);
                    devices.append(devInfo);
                }
            }
        }
        m_closeDevice(dh);
    }
#endif

    return devices;
}

// ═══════════════════════════════════════════════════════════════
// 打开 / 关闭
// ═══════════════════════════════════════════════════════════════

UINT ZcanFdAdapter::baudToTiming(CanBaudRate baud) const
{
    // ZCANFD CAN FD 使用 timing 值 (与 SJA1000 兼容)
    // 格式: 低16位=arbitration timing, 高16位=data timing
    // timing0: SJW(7:6) | BRP(5:0)
    // timing1: SAM(7) | TSEG2(6:4) | TSEG1(3:0)
    switch (baud) {
    case CanBaudRate::BR_1M:   return 0x00140014;  // 1M
    case CanBaudRate::BR_800K: return 0x00160016;  // 800K
    case CanBaudRate::BR_500K: return 0x001C001C;  // 500K
    case CanBaudRate::BR_250K: return 0x011C011C;  // 250K
    case CanBaudRate::BR_125K: return 0x031C031C;  // 125K
    case CanBaudRate::BR_100K: return 0x432F432F;  // 100K
    case CanBaudRate::BR_50K:  return 0x472F472F;  // 50K
    case CanBaudRate::BR_20K:  return 0x532F532F;  // 20K
    case CanBaudRate::BR_10K:  return 0x672F672F;  // 10K
    case CanBaudRate::BR_5K:   return 0x7F7F7F7F;  // 5K
    default: return 0x001C001C;
    }
}

bool ZcanFdAdapter::open(int channel, CanBaudRate baud)
{
    if (m_opened) close();

    UINT devIdx = (channel >> 8) & 0xFF;
    UINT chIdx  = channel & 0xFF;

    // ── 打开设备 ──
#ifndef Q_OS_WIN
    m_devHandle = ZCAN_OpenDevice(m_deviceType, devIdx, 0);
#else
    if (!m_loaded) return false;
    m_devHandle = m_openDevice(m_deviceType, devIdx, 0);
#endif

    if (!m_devHandle || m_devHandle == INVALID_DEVICE_HANDLE) {
        emit errorOccurred(QString("ZCANFD: 打开设备 #%1 失败").arg(devIdx));
        return false;
    }

    // ── 初始化 CAN 通道 ──
    ZCAN_CHANNEL_INIT_CONFIG initConfig;
    memset(&initConfig, 0, sizeof(initConfig));
    initConfig.can_type = TYPE_CAN; // 默认标准 CAN, 后续根据需要切换 CAN FD
    initConfig.canfd.abit_timing = baudToTiming(baud);
    initConfig.canfd.dbit_timing = baudToTiming(CanBaudRate::BR_1M); // 数据域默认 1M
    initConfig.canfd.acc_code = 0;
    initConfig.canfd.acc_mask = 0xFFFFFFFF;
    initConfig.canfd.filter = 1;  // 接收所有帧
    initConfig.canfd.mode = 0;    // 正常模式

#ifndef Q_OS_WIN
    m_chHandle = ZCAN_InitCAN(m_devHandle, chIdx, &initConfig);
#else
    m_chHandle = m_initCAN(m_devHandle, chIdx, &initConfig);
#endif

    if (!m_chHandle || m_chHandle == INVALID_CHANNEL_HANDLE) {
        emit errorOccurred(QString("ZCANFD: 初始化 CAN 通道 %1 失败").arg(chIdx));
#ifndef Q_OS_WIN
        ZCAN_CloseDevice(m_devHandle);
#else
        m_closeDevice(m_devHandle);
#endif
        m_devHandle = nullptr;
        return false;
    }

    // ── 启动 CAN ──
    UINT ret;
#ifndef Q_OS_WIN
    ret = ZCAN_StartCAN(m_chHandle);
#else
    ret = m_startCAN(m_chHandle);
#endif

    if (ret != STATUS_OK) {
        emit errorOccurred(QString("ZCANFD: 启动 CAN 通道 %1 失败 (0x%2)")
                           .arg(chIdx).arg(ret, 4, 16, QChar('0')));
#ifndef Q_OS_WIN
        ZCAN_CloseDevice(m_devHandle);
#else
        m_closeDevice(m_devHandle);
#endif
        m_devHandle = nullptr;
        m_chHandle = nullptr;
        return false;
    }

    m_deviceIndex = devIdx;
    m_canIndex = chIdx;
    m_opened = true;

    // ── 启动读取轮询 ──
    if (!m_readTimer) {
        m_readTimer = new QTimer(this);
        connect(m_readTimer, &QTimer::timeout, this, &ZcanFdAdapter::onReadTimer);
    }
    m_readTimer->start(1); // 1ms 轮询

    return true;
}

void ZcanFdAdapter::close()
{
    if (m_readTimer) {
        m_readTimer->stop();
    }

    if (m_chHandle) {
#ifndef Q_OS_WIN
        ZCAN_ResetCAN(m_chHandle);
#else
        if (m_resetCAN) m_resetCAN(m_chHandle);
#endif
        m_chHandle = nullptr;
    }

    if (m_devHandle) {
#ifndef Q_OS_WIN
        ZCAN_CloseDevice(m_devHandle);
#else
        if (m_closeDevice) m_closeDevice(m_devHandle);
#endif
        m_devHandle = nullptr;
    }

    m_opened = false;
}

bool ZcanFdAdapter::isOpen() const
{
    return m_opened;
}

bool ZcanFdAdapter::isAlive() const
{
    if (!m_opened || !m_devHandle) return false;

#ifndef Q_OS_WIN
    UINT ret = ZCAN_IsDeviceOnLine(m_devHandle);
    return (ret == STATUS_ONLINE || ret == STATUS_OK);
#else
    // Windows DLL 没有直接导出 IsDeviceOnLine, 通过发送/接收间接判断
    return m_opened;
#endif
}

// ═══════════════════════════════════════════════════════════════
// 读取轮询
// ═══════════════════════════════════════════════════════════════

void ZcanFdAdapter::onReadTimer()
{
    if (!m_opened || !m_chHandle) return;

    // 先尝试 CAN FD 接收, 再尝试标准 CAN 接收
    pollMessages();
}

void ZcanFdAdapter::pollMessages()
{
    // ── 尝试 CAN FD 接收 ──
    ZCAN_ReceiveFD_Data fdData[16];
    memset(fdData, 0, sizeof(fdData));

#ifndef Q_OS_WIN
    UINT fdCount = ZCAN_ReceiveFD(m_chHandle, fdData, 16, 0); // 0ms 超时
#else
    UINT fdCount = 0;
    if (m_receiveFD)
        fdCount = m_receiveFD(m_chHandle, fdData, 16, 0);
#endif

    for (UINT i = 0; i < fdCount; ++i) {
        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = m_canIndex;
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

#ifndef Q_OS_WIN
    UINT canCount = ZCAN_Receive(m_chHandle, canData, 16, 0);
#else
    UINT canCount = m_receive(m_chHandle, canData, 16, 0);
#endif

    for (UINT i = 0; i < canCount; ++i) {
        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = m_canIndex;
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
}

// ═══════════════════════════════════════════════════════════════
// 发送
// ═══════════════════════════════════════════════════════════════

bool ZcanFdAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened || !m_chHandle) return false;

    if (msg.isFd && msg.dlc > 8) {
        // ── CAN FD 发送 ──
        ZCAN_TransmitFD_Data fdData;
        memset(&fdData, 0, sizeof(fdData));
        fdData.transmit_type = 0; // 正常发送

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
#ifndef Q_OS_WIN
        ret = ZCAN_TransmitFD(m_chHandle, &fdData, 1);
#else
        ret = m_transmitFD ? m_transmitFD(m_chHandle, &fdData, 1) : STATUS_ERR;
#endif
        if (ret != STATUS_OK) {
            emit errorOccurred(QString("ZCANFD: CAN FD 发送失败 (0x%1)").arg(ret, 4, 16, QChar('0')));
            return false;
        }
    } else {
        // ── 标准 CAN 发送 ──
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
#ifndef Q_OS_WIN
        ret = ZCAN_Transmit(m_chHandle, &canData, 1);
#else
        ret = m_transmit(m_chHandle, &canData, 1);
#endif
        if (ret != STATUS_OK) {
            emit errorOccurred(QString("ZCANFD: 发送失败 (0x%1)").arg(ret, 4, 16, QChar('0')));
            return false;
        }
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════
// 辅助
// ═══════════════════════════════════════════════════════════════

QString ZcanFdAdapter::channelName(int channel)
{
    UINT devIdx = (channel >> 8) & 0xFF;
    UINT chIdx  = channel & 0xFF;
    return QString("ZCANFD#%1_CH%2").arg(devIdx).arg(chIdx);
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
