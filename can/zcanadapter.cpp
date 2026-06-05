#include "zcanadapter.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════

ZcanAdapter::ZcanAdapter(QObject *parent)
    : CanInterface(parent)
{
    loadLibrary();
}

ZcanAdapter::~ZcanAdapter()
{
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    close();
    if (m_loaded) {
        unloadLibrary();
    } else if (m_library) {
        delete m_library;
        m_library = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════
// 动态加载 ControlCAN.dll
// ═══════════════════════════════════════════════════════════════

bool ZcanAdapter::loadLibrary()
{
    if (m_loaded) return true;

    m_library = new QLibrary("ControlCAN");
    if (!m_library->load()) {
        m_library->setFileName("third_party/zcan/ControlCAN.dll");
        if (!m_library->load()) {
            qWarning() << "ControlCAN.dll 加载失败 (无ZCAN驱动):" << m_library->errorString();
            delete m_library;
            m_library = nullptr;
            return false;
        }
    }

    m_openDevice    = (VCI_OpenDevice_t)    m_library->resolve("VCI_OpenDevice");
    m_closeDevice   = (VCI_CloseDevice_t)   m_library->resolve("VCI_CloseDevice");
    m_initCAN       = (VCI_InitCAN_t)       m_library->resolve("VCI_InitCAN");
    m_readBoardInfo = (VCI_ReadBoardInfo_t) m_library->resolve("VCI_ReadBoardInfo");
    m_startCAN      = (VCI_StartCAN_t)      m_library->resolve("VCI_StartCAN");
    m_resetCAN      = (VCI_ResetCAN_t)      m_library->resolve("VCI_ResetCAN");
    m_getReceiveNum = (VCI_GetReceiveNum_t) m_library->resolve("VCI_GetReceiveNum");
    m_clearBuffer   = (VCI_ClearBuffer_t)   m_library->resolve("VCI_ClearBuffer");
    m_transmit      = (VCI_Transmit_t)      m_library->resolve("VCI_Transmit");
    m_receive       = (VCI_Receive_t)       m_library->resolve("VCI_Receive");

    if (!m_openDevice || !m_closeDevice || !m_initCAN
        || !m_startCAN || !m_transmit || !m_receive) {
        qWarning() << "ControlCAN.dll 函数解析失败";
        unloadLibrary();
        return false;
    }

    m_loaded = true;
    return true;
}

void ZcanAdapter::unloadLibrary()
{
    if (m_library) {
        m_library->unload();
        delete m_library;
        m_library = nullptr;
    }
    m_openDevice = nullptr;
    m_closeDevice = nullptr;
    m_initCAN = nullptr;
    m_readBoardInfo = nullptr;
    m_startCAN = nullptr;
    m_resetCAN = nullptr;
    m_getReceiveNum = nullptr;
    m_clearBuffer = nullptr;
    m_transmit = nullptr;
    m_receive = nullptr;
    m_loaded = false;
}

// ═══════════════════════════════════════════════════════════════
// 扫描设备
// ═══════════════════════════════════════════════════════════════

QList<CanDeviceInfo> ZcanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;
    if (!m_loaded) return devices;

    // 依次尝试 USBCAN1, USBCAN2, USBCAN-E-U, USBCAN-2E-U
    static const DWORD deviceTypes[] = {
        VCI_USBCAN1, VCI_USBCAN2, VCI_USBCAN_E_U, VCI_USBCAN_2E_U
    };

    for (DWORD devType : deviceTypes) {
        for (DWORD devIdx = 0; devIdx < 16; ++devIdx) {
            // 尝试打开设备
            DWORD ret = m_openDevice(devType, devIdx, 0);
            if (ret != 1) // STATUS_OK
                continue;

            // 读取设备信息
            VCI_BOARD_INFO info;
            memset(&info, 0, sizeof(info));
            bool hasInfo = false;
            if (m_readBoardInfo) {
                DWORD infoRet = m_readBoardInfo(devType, devIdx, &info);
                if (infoRet == 1) { // STATUS_OK
                    info.str_Serial_Num[sizeof(info.str_Serial_Num) - 1] = '\0';
                    info.str_hw_Type[sizeof(info.str_hw_Type) - 1] = '\0';
                    hasInfo = true;
                }
            }

            for (DWORD ch = 0; ch < 2; ++ch) {
                CanDeviceInfo devInfo;
                devInfo.channel = (devType << 16) | (devIdx << 8) | ch;
                devInfo.adapterType = static_cast<int>(CanAdapterType::ZCAN);

                if (hasInfo) {
                    QString hwType = QString::fromLatin1(info.str_hw_Type);
                    devInfo.name = QString("ZCAN #%1 CH%2").arg(devIdx).arg(ch);
                    devInfo.description = QString("%1 SN:%2 [CH%3]")
                        .arg(hwType.isEmpty() ? "USBCAN" : hwType)
                        .arg(info.str_Serial_Num)
                        .arg(ch);
                } else {
                    devInfo.name = QString("ZCAN #%1 CH%2").arg(devIdx).arg(ch);
                    devInfo.description = QString("USBCAN Dev%1 [CH%2]").arg(devIdx).arg(ch);
                }
                devices.append(devInfo);
            }

            m_closeDevice(devType, devIdx);
        }
    }

    return devices;
}

// ═══════════════════════════════════════════════════════════════
// 波特率 → Timing0/Timing1 (SJA1000 16MHz)
// ═══════════════════════════════════════════════════════════════

UINT ZcanAdapter::timing0ForBaud(CanBaudRate baud) const
{
    switch (baud) {
    case CanBaudRate::BR_1M:   return 0x00;  // SJW=1, BRP=1
    case CanBaudRate::BR_800K: return 0x00;  // SJW=1, BRP=1
    case CanBaudRate::BR_500K: return 0x00;  // SJW=1, BRP=1
    case CanBaudRate::BR_250K: return 0x01;  // SJW=1, BRP=2
    case CanBaudRate::BR_125K: return 0x03;  // SJW=1, BRP=4
    case CanBaudRate::BR_100K: return 0x04;  // SJW=1, BRP=5
    case CanBaudRate::BR_50K:  return 0x09;  // SJW=1, BRP=10
    case CanBaudRate::BR_20K:  return 0x18;  // SJW=1, BRP=25
    case CanBaudRate::BR_10K:  return 0x31;  // SJW=1, BRP=50
    case CanBaudRate::BR_5K:   return 0x63;  // SJW=1, BRP=100
    default: return 0x00;
    }
}

UINT ZcanAdapter::timing1ForBaud(CanBaudRate baud) const
{
    switch (baud) {
    case CanBaudRate::BR_1M:   return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_800K: return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_500K: return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_250K: return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_125K: return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_100K: return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_50K:  return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_20K:  return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_10K:  return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    case CanBaudRate::BR_5K:   return 0x1C;  // SAM=0, TSEG2=3, TSEG1=12
    default: return 0x1C;
    }
}

// ═══════════════════════════════════════════════════════════════
// 打开 / 关闭
// ═══════════════════════════════════════════════════════════════

bool ZcanAdapter::open(int channel, CanBaudRate baud)
{
    if (m_opened) close();
    if (!m_loaded) return false;

    // 解码通道: bit[31:24]=type, bit[23:16]=devIdx, bit[7:0]=chIdx
    DWORD devType = (channel >> 16) & 0xFF;
    DWORD devIdx  = (channel >> 8) & 0xFF;
    DWORD chIdx   = channel & 0xFF;

    // 修复: 某些设备类型可能已经在通道码中编码为 0
    if (devType == 0)
        devType = VCI_USBCAN2;

    // ── 打开设备 ──
    DWORD ret = m_openDevice(devType, devIdx, 0);
    if (ret != 1) { // STATUS_OK
        emit errorOccurred(QString("ZCAN: 打开设备 type=%1 idx=%2 失败 (0x%3)")
                           .arg(devType).arg(devIdx).arg(ret, 8, 16, QChar('0')));
        return false;
    }

    // ── 初始化 CAN 通道 ──
    VCI_INIT_CONFIG initConfig;
    memset(&initConfig, 0, sizeof(initConfig));
    initConfig.AccCode = 0;
    initConfig.AccMask = 0xFFFFFFFF;
    initConfig.Filter  = 1;   // 接收所有帧
    initConfig.Timing0 = timing0ForBaud(baud);
    initConfig.Timing1 = timing1ForBaud(baud);
    initConfig.Mode    = 0;   // 正常模式

    ret = m_initCAN(devType, devIdx, chIdx, &initConfig);
    if (ret != 1) { // STATUS_OK
        emit errorOccurred(QString("ZCAN: 初始化 CAN ch=%1 失败 (0x%2)")
                           .arg(chIdx).arg(ret, 8, 16, QChar('0')));
        m_closeDevice(devType, devIdx);
        return false;
    }

    // ── 清除缓冲区 ──
    if (m_clearBuffer)
        m_clearBuffer(devType, devIdx, chIdx);

    // ── 启动 CAN ──
    ret = m_startCAN(devType, devIdx, chIdx);
    if (ret != 1) { // STATUS_OK
        emit errorOccurred(QString("ZCAN: 启动 CAN ch=%1 失败 (0x%2)")
                           .arg(chIdx).arg(ret, 8, 16, QChar('0')));
        m_closeDevice(devType, devIdx);
        return false;
    }

    m_deviceType  = devType;
    m_deviceIndex = devIdx;
    m_canIndex    = chIdx;
    m_opened      = true;

    // ── 启动读取轮询 ──
    if (!m_readTimer) {
        m_readTimer = new QTimer(this);
        connect(m_readTimer, &QTimer::timeout, this, &ZcanAdapter::onReadTimer);
    }
    m_readTimer->start(1); // 1ms 轮询

    return true;
}

void ZcanAdapter::close()
{
    if (m_readTimer) {
        m_readTimer->stop();
    }

    if (m_opened) {
        m_resetCAN(m_deviceType, m_deviceIndex, m_canIndex);
        m_closeDevice(m_deviceType, m_deviceIndex);
        m_opened = false;
    }
}

bool ZcanAdapter::isOpen() const
{
    return m_opened;
}

bool ZcanAdapter::isAlive() const
{
    // VCI API 没有独立的存活检测，通过发送/接收状态间接判断
    return m_opened;
}

// ═══════════════════════════════════════════════════════════════
// 读取轮询
// ═══════════════════════════════════════════════════════════════

void ZcanAdapter::onReadTimer()
{
    if (!m_opened) return;
    pollMessages();
}

void ZcanAdapter::pollMessages()
{
    VCI_CAN_OBJ frames[16];
    memset(frames, 0, sizeof(frames));

    ULONG count = m_receive(m_deviceType, m_deviceIndex, m_canIndex,
                            frames, 16, 0); // 0ms 超时

    for (ULONG i = 0; i < count; ++i) {
        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = m_canIndex;
        msg.timestamp = QDateTime::currentDateTime();
        msg.isFd = false;

        if (frames[i].ExternFlag)
            msg.type = CanFrameType::ExtendedData;
        else if (frames[i].RemoteFlag)
            msg.type = CanFrameType::Remote;
        else
            msg.type = CanFrameType::StandardData;

        msg.id = frames[i].ID;
        msg.dlc = frames[i].DataLen > 8 ? 8 : frames[i].DataLen;

        for (int j = 0; j < msg.dlc; ++j)
            msg.data[j] = frames[i].Data[j];

        emit messageReceived(msg);
    }
}

// ═══════════════════════════════════════════════════════════════
// 发送
// ═══════════════════════════════════════════════════════════════

bool ZcanAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened) return false;

    VCI_CAN_OBJ frame;
    memset(&frame, 0, sizeof(frame));

    frame.ID = msg.id;

    if (msg.type == CanFrameType::ExtendedData)
        frame.ExternFlag = 1;
    else if (msg.type == CanFrameType::Remote)
        frame.RemoteFlag = 1;

    frame.DataLen = msg.dlc > 8 ? 8 : msg.dlc;
    for (int j = 0; j < frame.DataLen; ++j)
        frame.Data[j] = msg.data[j];

    // VCI_Transmit 返回实际发送帧数
    ULONG ret = m_transmit(m_deviceType, m_deviceIndex, m_canIndex,
                           &frame, 1);
    if (ret != 1) {
        emit errorOccurred(QString("ZCAN: 发送失败, 返回值=%1").arg(ret));
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════
// 辅助
// ═══════════════════════════════════════════════════════════════

QString ZcanAdapter::channelName(int channel)
{
    DWORD devIdx = (channel >> 8) & 0xFF;
    DWORD chIdx  = channel & 0xFF;
    return QString("ZCAN#%1_CH%2").arg(devIdx).arg(chIdx);
}
