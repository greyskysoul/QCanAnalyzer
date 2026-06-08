#include "zcanadapter.h"

// ControlCAN.h 需要的 Windows 类型
typedef unsigned char       BYTE;
typedef char                CHAR;
typedef unsigned char       UCHAR;
typedef int                 INT;
typedef unsigned int        UINT;
typedef unsigned short      USHORT;
typedef unsigned long       DWORD;
typedef unsigned long       ULONG;
typedef void*               PVOID;

#include "ControlCAN.h"
#include <QDebug>
#include <QDateTime>
#include <cstring>

// 静态缓存: ZCAN 设备打开后禁止扫描 (FindUsbDevice2 会断开已打开设备)
int ZcanAdapter::s_openCount = 0;
QSet<unsigned long> ZcanAdapter::s_openDeviceIndices = {};
QList<CanDeviceInfo> ZcanAdapter::s_cachedDevices = {};

// ═══════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════

ZcanAdapter::ZcanAdapter(QObject *parent)
    : CanInterface(parent)
{
}

ZcanAdapter::~ZcanAdapter()
{
    if (m_readTimer) {
        m_readTimer->stop();
        delete m_readTimer;
        m_readTimer = nullptr;
    }
    close();
}

// ═══════════════════════════════════════════════════════════════
// 扫描设备
// ═══════════════════════════════════════════════════════════════

QList<CanDeviceInfo> ZcanAdapter::scanDevices()
{
    // 设备已打开时返回缓存, 禁止实际扫描
    // VCI_FindUsbDevice2 会断开已打开的 ZCAN 设备
    if (s_openCount > 0) {
        // 过滤掉已被其他会话占用的设备, 防止重复打开
        QList<CanDeviceInfo> filtered;
        for (const auto &dev : s_cachedDevices) {
            if (!s_openDeviceIndices.contains(static_cast<unsigned long>(dev.deviceIndex)))
                filtered.append(dev);
        }
        return filtered;
    }

    QList<CanDeviceInfo> devices;

    VCI_BOARD_INFO infoArray[50];
    memset(infoArray, 0, sizeof(infoArray));
    DWORD count = VCI_FindUsbDevice2(infoArray);
    if (count == 0 || count > 50) return devices;

    for (DWORD i = 0; i < count; ++i) {
        VCI_BOARD_INFO &info = infoArray[i];
        info.str_Serial_Num[sizeof(info.str_Serial_Num)-1] = '\0';
        info.str_hw_Type[sizeof(info.str_hw_Type)-1] = '\0';
        QString hwType = QString::fromLatin1(info.str_hw_Type);
        QString serial = QString::fromLatin1(info.str_Serial_Num);
        int cc = (info.can_Num > 0 && info.can_Num <= 8) ? info.can_Num : 2;

        CanDeviceInfo di;
        di.channel = static_cast<int>((VCI_USBCAN2 << 16) | (i << 8));
        di.adapterType = static_cast<int>(CanAdapterType::ZCAN);
        di.deviceType = VCI_USBCAN2;
        di.deviceIndex = static_cast<int>(i);
        di.channelCount = cc;
        di.name = QString("ZCAN #%1").arg(i);
        di.description = QString("%1 SN:%2")
            .arg(hwType.isEmpty() ? "USBCAN" : hwType)
            .arg(serial);
        devices.append(di);
    }

    s_cachedDevices = devices;
    return devices;
}

// ═══════════════════════════════════════════════════════════════
// 波特率 → Timing0/Timing1 (SJA1000 16MHz)
// ═══════════════════════════════════════════════════════════════

UINT ZcanAdapter::timing0ForBaud(CanBaudRate baud) const
{
    // SJA1000 @ 16MHz — 参照官方 CAN-DEMO-Qt-V1.2
    switch (baud) {
    case CanBaudRate::BR_1M:   return 0x00;
    case CanBaudRate::BR_800K: return 0x00;
    case CanBaudRate::BR_500K: return 0x00;
    case CanBaudRate::BR_250K: return 0x01;
    case CanBaudRate::BR_125K: return 0x03;
    case CanBaudRate::BR_100K: return 0x04;
    case CanBaudRate::BR_50K:  return 0x09;
    case CanBaudRate::BR_20K:  return 0x18;
    case CanBaudRate::BR_10K:  return 0x31;
    case CanBaudRate::BR_5K:   return 0x63;
    default: return 0x00;
    }
}

UINT ZcanAdapter::timing1ForBaud(CanBaudRate baud) const
{
    // SJA1000 @ 16MHz — 参照官方 CAN-DEMO-Qt-V1.2
    switch (baud) {
    case CanBaudRate::BR_1M:   return 0x14;
    case CanBaudRate::BR_800K: return 0x16;
    case CanBaudRate::BR_500K: return 0x1C;
    case CanBaudRate::BR_250K: return 0x1C;
    case CanBaudRate::BR_125K: return 0x1C;
    case CanBaudRate::BR_100K: return 0x1C;
    case CanBaudRate::BR_50K:  return 0x1C;
    case CanBaudRate::BR_20K:  return 0x1C;
    case CanBaudRate::BR_10K:  return 0x1C;
    case CanBaudRate::BR_5K:   return 0x1C;
    default: return 0x1C;
    }
}

// ═══════════════════════════════════════════════════════════════
// 打开 / 关闭
// ═══════════════════════════════════════════════════════════════

bool ZcanAdapter::open(int channel, CanBaudRate baud)
{
    if (m_opened) close();


    // 解码通道: bit[31:24]=type, bit[23:16]=devIdx, bit[7:0]=chIdx
    DWORD devType = (channel >> 16) & 0xFF;
    DWORD devIdx  = (channel >> 8) & 0xFF;
    DWORD chIdx   = channel & 0xFF;

    if (devType == 0)
        devType = VCI_USBCAN2;

    // ── 重复打开检测: 同一设备索引只能被一个实例打开 ──
    if (s_openDeviceIndices.contains(devIdx)) {
        emit errorOccurred(QString("ZCAN: 设备 #%1 已被打开, 不能重复打开").arg(devIdx));
        return false;
    }

    qDebug() << "[ZCAN] open() raw channel=" << Qt::hex << channel
             << "decoded: devType=" << devType << "devIdx=" << devIdx << "chIdx=" << chIdx
             << "baud=" << baudRateString(baud);

    // ── 打开设备 ──
    DWORD ret = VCI_OpenDevice(devType, devIdx, 0);
    if (ret != 1) {
        emit errorOccurred(QString("ZCAN: 打开设备 type=%1 idx=%2 失败 (0x%3)")
                           .arg(devType).arg(devIdx).arg(ret, 8, 16, QChar('0')));
        return false;
    }

    m_deviceType  = devType;
    m_deviceIndex = devIdx;
    m_totalChannels = 2;

    // ── 严格参照官方 CAN-DEMO-Qt-V1.2 的 initCAN + startCAN 流程 ──

    // 1. ClearBuffer (InitCAN 之前)
    VCI_ClearBuffer(devType, devIdx, 0);
    VCI_ClearBuffer(devType, devIdx, 1);

    // 2. InitCAN (ch0 先, ch1 后)
    VCI_INIT_CONFIG cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.AccCode = 0;
    cfg.AccMask = 0xFFFFFFFF;
    cfg.Filter  = 1;
    cfg.Timing0 = timing0ForBaud(baud);
    cfg.Timing1 = timing1ForBaud(baud);
    cfg.Mode    = 0;

    DWORD ret0 = VCI_InitCAN(devType, devIdx, 0, &cfg);
    DWORD ret1 = VCI_InitCAN(devType, devIdx, 1, &cfg);
    if (ret0 != 1 || ret1 != 1) {
        emit errorOccurred(QString("ZCAN: InitCAN 失败 ch0=%1 ch1=%2").arg(ret0).arg(ret1));
        VCI_CloseDevice(devType, devIdx);
        return false;
    }

    // 3. ReadBoardInfo (InitCAN 之后)
    VCI_BOARD_INFO info;
    memset(&info, 0, sizeof(info));
    if (VCI_ReadBoardInfo(devType, devIdx, &info) == 1
        && info.can_Num > 0 && info.can_Num <= 8)
        m_totalChannels = info.can_Num;

    // 4. StartCAN (ch0 先, ch1 后)
    if (VCI_StartCAN(devType, devIdx, 0) != 1) {
        emit errorOccurred("ZCAN: StartCAN ch0 失败");
        VCI_CloseDevice(devType, devIdx);
        return false;
    }
    if (m_totalChannels > 1 && VCI_StartCAN(devType, devIdx, 1) != 1) {
        emit errorOccurred("ZCAN: StartCAN ch1 失败");
        VCI_CloseDevice(devType, devIdx);
        return false;
    }

    m_openChannels.clear();
    for (int ch = 0; ch < m_totalChannels; ++ch) {
        ChannelInfo ci;
        ci.chIdx = static_cast<DWORD>(ch);
        ci.baud = baud;
        m_openChannels.append(ci);
    }

    m_canIndex = chIdx;
    m_opened = true;
    ++s_openCount;  // 引用计数+1, 禁止扫描
    s_openDeviceIndices.insert(m_deviceIndex);

    if (!m_readTimer) {
        m_readTimer = new QTimer(this);
        connect(m_readTimer, &QTimer::timeout, this, &ZcanAdapter::onReadTimer);
    }
    m_readTimer->start(1);

    return true;
}

void ZcanAdapter::close()
{
    if (!m_opened) return;

    if (m_readTimer) {
        m_readTimer->stop();
    }

    if (m_opened) {
        // 复位当前通道
        for (const auto &ci : m_openChannels) {
            VCI_ResetCAN(m_deviceType, m_deviceIndex, ci.chIdx);
        }
        m_openChannels.clear();
        VCI_CloseDevice(m_deviceType, m_deviceIndex);
        m_opened = false;
        s_openDeviceIndices.remove(m_deviceIndex);
        if (s_openCount > 0) --s_openCount;
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
    for (const auto &ci : m_openChannels) {
        VCI_CAN_OBJ frames[16];
        memset(frames, 0, sizeof(frames));

        ULONG count = VCI_Receive(m_deviceType, m_deviceIndex, ci.chIdx,
                                frames, 16, 0);

        for (ULONG i = 0; i < count; ++i) {
            CanMessage msg;
            msg.direction = CanDirection::Rx;
            msg.channel = static_cast<int>(ci.chIdx);
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
}

// ═══════════════════════════════════════════════════════════════
// 发送
// ═══════════════════════════════════════════════════════════════

bool ZcanAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened) return false;

    DWORD sendCh = m_canIndex;
    if (msg.channel >= 0 && msg.channel < m_totalChannels)
        sendCh = static_cast<DWORD>(msg.channel);

    // 完全参照官方 CAN-DEMO-Qt-V1.2 的 sendData
    VCI_CAN_OBJ vco;
    vco.ID = msg.id;
    vco.RemoteFlag = (msg.type == CanFrameType::Remote) ? 1 : 0;
    vco.ExternFlag = (msg.type == CanFrameType::ExtendedData) ? 1 : 0;
    vco.DataLen = msg.dlc > 8 ? 8 : msg.dlc;
    for (int j = 0; j < vco.DataLen; ++j)
        vco.Data[j] = msg.data[j];

    if (VCI_Transmit(m_deviceType, m_deviceIndex, sendCh, &vco, 1) > 0)
        return true;

    emit errorOccurred(QString("ZCAN: 发送失败 devType=%1 idx=%2 ch=%3")
                       .arg(m_deviceType).arg(m_deviceIndex).arg(sendCh));
    return false;
}

// ═══════════════════════════════════════════════════════════════
// 多通道支持
// ═══════════════════════════════════════════════════════════════

QList<int> ZcanAdapter::availableSendChannels() const
{
    // 返回设备所有可用通道 (不仅仅是当前打开的)
    QList<int> channels;
    int count = m_totalChannels > 0 ? m_totalChannels : 1;
    for (int ch = 0; ch < count; ++ch)
        channels.append(ch);
    return channels;
}

bool ZcanAdapter::setSendChannel(int channel)
{
    if (channel < 0 || channel >= m_totalChannels) return false;
    if (!m_opened) return false;

    for (const auto &ci : m_openChannels) {
        if (static_cast<int>(ci.chIdx) == channel) {
            m_canIndex = static_cast<DWORD>(channel);
            return true;
        }
    }
    return false;
}

int ZcanAdapter::currentSendChannel() const
{
    return static_cast<int>(m_canIndex);
}

// ═══════════════════════════════════════════════════════════════
// 辅助
// ═══════════════════════════════════════════════════════════════

QString ZcanAdapter::channelName(int channel)
{
    DWORD devIdx = (channel >> 8) & 0xFF;
    return QString("ZCAN #%1").arg(devIdx);
}
