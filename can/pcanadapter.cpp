#include "pcanadapter.h"
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

// ─── 构造 / 析构 ──────────────────────────────────────────────

PcanAdapter::PcanAdapter(QObject *parent)
    : CanInterface(parent)
{
    loadLibrary();
}

PcanAdapter::~PcanAdapter()
{
    close();
    if (m_loaded) {
        unloadLibrary();
    } else if (m_library) {
        delete m_library;
        m_library = nullptr;
    }
}

// ─── 动态加载 PCANBasic.dll ───────────────────────────────────

bool PcanAdapter::loadLibrary()
{
    if (m_loaded) return true;

    // 不设 parent: 栈上对象析构顺序不确定可能导致问题
    m_library = new QLibrary("PCANBasic");
    if (!m_library->load()) {
        qWarning() << "PCANBasic.dll 加载失败 (无PCAN驱动):" << m_library->errorString();
        delete m_library;
        m_library = nullptr;
        return false;
    }

    m_Initialize     = (CAN_Initialize_t)    m_library->resolve("CAN_Initialize");
    m_Uninitialize   = (CAN_Uninitialize_t)  m_library->resolve("CAN_Uninitialize");
    m_Reset          = (CAN_Reset_t)         m_library->resolve("CAN_Reset");
    m_GetStatus      = (CAN_GetStatus_t)     m_library->resolve("CAN_GetStatus");
    m_Read           = (CAN_Read_t)          m_library->resolve("CAN_Read");
    m_Write          = (CAN_Write_t)         m_library->resolve("CAN_Write");
    m_FilterMessages = (CAN_FilterMessages_t)m_library->resolve("CAN_FilterMessages");
    m_GetValue       = (CAN_GetValue_t)      m_library->resolve("CAN_GetValue");
    m_SetValue       = (CAN_SetValue_t)      m_library->resolve("CAN_SetValue");
    m_GetErrorText   = (CAN_GetErrorText_t)  m_library->resolve("CAN_GetErrorText");

    if (!m_Initialize || !m_Uninitialize || !m_Read || !m_Write || !m_GetStatus) {
        qWarning() << "PCANBasic.dll 函数解析失败";
        unloadLibrary();
        return false;
    }

    m_loaded = true;
    return true;
}

void PcanAdapter::unloadLibrary()
{
    if (m_library) {
        m_library->unload();
        delete m_library;
        m_library = nullptr;
    }
    m_Initialize = nullptr;
    m_Uninitialize = nullptr;
    m_Reset = nullptr;
    m_GetStatus = nullptr;
    m_Read = nullptr;
    m_Write = nullptr;
    m_FilterMessages = nullptr;
    m_GetValue = nullptr;
    m_SetValue = nullptr;
    m_GetErrorText = nullptr;
    m_loaded = false;
}

// ─── 扫描设备 ─────────────────────────────────────────────────

QList<CanDeviceInfo> PcanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;
    if (!m_loaded) return devices;

    // ═══ 方法1: 通过 CAN_GetValue 查询已连接 (attached) 通道 ═══
    // PCAN_ATTACHED_CHANNELS 返回一个 uint32_t 位掩码
    if (m_GetValue) {
        uint32_t attachedMask = 0;
        uint32_t res = m_GetValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS,
                                  &attachedMask, sizeof(attachedMask));
        if (res == PCAN_ERROR_OK && attachedMask != 0) {
            // 所有标准通道
            static const int channels[] = {
                PCAN_USBBUS1, PCAN_USBBUS2, PCAN_USBBUS3, PCAN_USBBUS4,
                PCAN_USBBUS5, PCAN_USBBUS6, PCAN_USBBUS7, PCAN_USBBUS8,
                PCAN_USBBUS9, PCAN_USBBUS10,PCAN_USBBUS11,PCAN_USBBUS12,
                PCAN_USBBUS13,PCAN_USBBUS14,PCAN_USBBUS15,PCAN_USBBUS16,
                PCAN_PCIBUS1, PCAN_PCIBUS2, PCAN_PCIBUS3, PCAN_PCIBUS4,
                PCAN_PCIBUS5, PCAN_PCIBUS6, PCAN_PCIBUS7, PCAN_PCIBUS8,
            };

            for (int ch : channels) {
                uint16_t handle = (uint16_t)ch;
                // 位掩码中对应位为1表示已连接
                if (attachedMask & (1u << (handle & 0x0F))) {
                    // 检查通道条件
                    uint32_t cond = 0;
                    m_GetValue(handle, PCAN_CHANNEL_CONDITION, &cond, sizeof(cond));

                    CanDeviceInfo info;
                    info.channel = ch;
                    info.name = channelName(ch);
                    QString condStr;
                    if (cond == PCAN_CHANNEL_AVAILABLE)
                        condStr = "可用";
                    else if (cond == PCAN_CHANNEL_OCCUPIED)
                        condStr = "被占用";
                    info.description = QString("%1 [%2]").arg(info.name).arg(condStr);
                    devices.append(info);
                }
            }
            return devices;
        }
    }

    // ═══ 方法2: 遍历通道尝试初始化 ═══
    // 有些驱动版本/旧版不支持 PCAN_ATTACHED_CHANNELS
    static const int fallbackChannels[] = {
        PCAN_USBBUS1, PCAN_USBBUS2, PCAN_USBBUS3, PCAN_USBBUS4,
        PCAN_USBBUS5, PCAN_USBBUS6, PCAN_USBBUS7, PCAN_USBBUS8,
        PCAN_USBBUS9, PCAN_USBBUS10,PCAN_USBBUS11,PCAN_USBBUS12,
        PCAN_USBBUS13,PCAN_USBBUS14,PCAN_USBBUS15,PCAN_USBBUS16,
        PCAN_PCIBUS1, PCAN_PCIBUS2, PCAN_PCIBUS3, PCAN_PCIBUS4,
        PCAN_PCIBUS5, PCAN_PCIBUS6, PCAN_PCIBUS7, PCAN_PCIBUS8,
    };

    for (int ch : fallbackChannels) {
        uint16_t handle = (uint16_t)ch;
        // 尝试初始化来检测硬件
        uint32_t res = m_Initialize(handle, 0x001C, 0, 0, 0);
        if (res == PCAN_ERROR_OK) {
            CanDeviceInfo info;
            info.channel = ch;
            info.name = channelName(ch);
            info.description = QString("%1 [可用]").arg(info.name);
            devices.append(info);
            m_Uninitialize(handle); // 立即释放
        }
        // PCAN_ERROR_ILLHW 和 PCAN_ERROR_NODRIVER 表示无硬件，跳过
    }

    return devices;
}

// ─── 打开 / 关闭 ──────────────────────────────────────────────

bool PcanAdapter::open(int channel, CanBaudRate baud)
{
    if (!m_loaded) return false;
    if (m_opened) close();

    uint32_t res = m_Initialize((TPCANHandle)channel, (TPCANBaudrate)baud, 0, 0, 0);
    if (res != PCAN_ERROR_OK) {
        emit errorOccurred(QString("PCAN 初始化失败: %1").arg(errorText(res)));
        return false;
    }

    // 设置读取超时
    if (m_SetValue) {
        uint32_t timeout = m_readTimeoutMs;
        m_SetValue((TPCANHandle)channel, PCAN_RECEIVE_EVENT, &timeout, sizeof(timeout));
    }

    m_channel = (uint16_t)channel;
    m_opened = true;

    // 启动轮询定时器 (复用, 避免内存泄漏)
    if (!m_readTimer) {
        m_readTimer = new QTimer(this);
        connect(m_readTimer, &QTimer::timeout, this, [this]() {
            if (!m_opened || !m_Read) return;
            TPCANMsg msg;
            TPCANTimestamp ts;
            uint32_t res;
            while ((res = m_Read(m_channel, &msg, &ts)) == PCAN_ERROR_OK) {
                CanMessage canMsg;
                canMsg.id = msg.ID;
                canMsg.dlc = msg.LEN;
                canMsg.isFd = false; // PCAN Basic API 不支持 CAN FD
                canMsg.direction = CanDirection::Rx;
                canMsg.channel = m_channel & 0x0F;  // 逻辑通道号
                canMsg.timestamp = QDateTime::currentDateTime(); // 使用本地时间

                if (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED)
                    canMsg.type = CanFrameType::ExtendedData;
                else if (msg.MSGTYPE & PCAN_MESSAGE_RTR)
                    canMsg.type = CanFrameType::Remote;
                else if (msg.MSGTYPE & PCAN_MESSAGE_STATUS)
                    canMsg.type = CanFrameType::Status;
                else
                    canMsg.type = CanFrameType::StandardData;

                for (int i = 0; i < 8 && i < (int)msg.LEN; ++i)
                    canMsg.data[i] = msg.DATA[i];

                emit messageReceived(canMsg);
            }
            if (res != PCAN_ERROR_OK && res != PCAN_ERROR_QRCVEMPTY) {
                // 总线错误日志
                if (res & PCAN_ERROR_BUSOFF) {
                    qWarning() << "PCAN: Bus-Off detected on channel" << m_channel;
                } else if (res & PCAN_ERROR_BUSHEAVY) {
                    qWarning() << "PCAN: Bus-Heavy warning on channel" << m_channel;
                } else if (res & PCAN_ERROR_BUSLIGHT) {
                    // Bus-Light: 轻度总线负载警告，静默处理
                }
                if (res == PCAN_ERROR_OVERRUN) {
                    qWarning() << "PCAN: Receive overrun on channel" << m_channel;
                } else if (res == PCAN_ERROR_QOVERRUN) {
                    qWarning() << "PCAN: Queue overrun on channel" << m_channel;
                }
            }
        });
    }
    m_readTimer->start(1); // 1ms 轮询
    return true;
}

void PcanAdapter::close()
{
    if (m_opened && m_Uninitialize) {
        m_Uninitialize(m_channel);
    }
    m_opened = false;
    m_channel = 0;
}

bool PcanAdapter::isOpen() const
{
    return m_opened;
}

bool PcanAdapter::isAlive() const
{
    if (!m_opened || !m_GetStatus) return false;

    uint32_t status = m_GetStatus(m_channel);
    // 如果返回 ILLHANDLE 或 NODRIVER 说明设备已物理断开
    if (status == PCAN_ERROR_ILLHANDLE ||
        status == PCAN_ERROR_NODRIVER ||
        status == PCAN_ERROR_ILLHW ||
        status == PCAN_ERROR_RESOURCE)
        return false;

    // 总线错误不影响存活判断
    return true;
}

// ─── 发送 ─────────────────────────────────────────────────────

bool PcanAdapter::sendMessage(const CanMessage &msg)
{
    if (!m_opened || !m_Write) return false;

    TPCANMsg pmsg;
    pmsg.ID = msg.id;
    pmsg.LEN = msg.dlc > 8 ? 8 : msg.dlc;
    if (msg.dlc > 8) {
        qWarning() << "PCAN: DLC truncated from" << msg.dlc << "to 8";
    }
    for (int i = 0; i < (int)pmsg.LEN; ++i)
        pmsg.DATA[i] = msg.data[i];

    pmsg.MSGTYPE = PCAN_MESSAGE_STANDARD;
    if (msg.type == CanFrameType::ExtendedData)
        pmsg.MSGTYPE = PCAN_MESSAGE_EXTENDED;
    else if (msg.type == CanFrameType::Remote)
        pmsg.MSGTYPE = PCAN_MESSAGE_RTR;

    uint32_t res = m_Write(m_channel, &pmsg);
    if (res != PCAN_ERROR_OK) {
        emit errorOccurred(QString("PCAN 发送失败: %1").arg(errorText(res)));
        return false;
    }
    return true;
}

// ─── 辅助 ─────────────────────────────────────────────────────

void PcanAdapter::setReadTimeout(int ms)
{
    m_readTimeoutMs = ms;
}

QString PcanAdapter::channelName(int channel)
{
    switch (channel) {
    case PCAN_USBBUS1: return "PCAN_USB1";
    case PCAN_USBBUS2: return "PCAN_USB2";
    case PCAN_USBBUS3: return "PCAN_USB3";
    case PCAN_USBBUS4: return "PCAN_USB4";
    case PCAN_USBBUS5: return "PCAN_USB5";
    case PCAN_USBBUS6: return "PCAN_USB6";
    case PCAN_USBBUS7: return "PCAN_USB7";
    case PCAN_USBBUS8: return "PCAN_USB8";
    case PCAN_USBBUS9: return "PCAN_USB9";
    case PCAN_USBBUS10: return "PCAN_USB10";
    case PCAN_USBBUS11: return "PCAN_USB11";
    case PCAN_USBBUS12: return "PCAN_USB12";
    case PCAN_USBBUS13: return "PCAN_USB13";
    case PCAN_USBBUS14: return "PCAN_USB14";
    case PCAN_USBBUS15: return "PCAN_USB15";
    case PCAN_USBBUS16: return "PCAN_USB16";
    case PCAN_PCIBUS1: return "PCAN_PCI1";
    case PCAN_PCIBUS2: return "PCAN_PCI2";
    case PCAN_PCIBUS3: return "PCAN_PCI3";
    case PCAN_PCIBUS4: return "PCAN_PCI4";
    case PCAN_PCIBUS5: return "PCAN_PCI5";
    case PCAN_PCIBUS6: return "PCAN_PCI6";
    case PCAN_PCIBUS7: return "PCAN_PCI7";
    case PCAN_PCIBUS8: return "PCAN_PCI8";
    default: return QString("PCAN_CH%1").arg(channel, 2, 16, QChar('0'));
    }
}

QString PcanAdapter::errorText(TPCANStatus err)
{
    if (!m_GetErrorText) return QString("0x%1").arg(err, 5, 16, QChar('0'));
    char buf[256] = {};
    m_GetErrorText(err, 0, buf);
    return QString::fromLatin1(buf);
}
