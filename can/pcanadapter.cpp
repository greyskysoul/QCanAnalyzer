#include "pcanadapter.h"
#include <QDebug>
#include <QDateTime>
#include <QTimer>
#include <QVector>

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

bool PcanAdapter::loadLibrary()
{
    if (m_loaded) return true;

    m_library = new QLibrary("PCANBasic");
    if (!m_library->load()) {
        qWarning() << "PCANBasic.dll 加载失败 (未安装 PCAN 驱动):" << m_library->errorString();
        delete m_library;
        m_library = nullptr;
        return false;
    }

    m_Initialize     = (CAN_Initialize_t)    m_library->resolve("CAN_Initialize");
    m_Uninitialize   = (CAN_Uninitialize_t)  m_library->resolve("CAN_Uninitialize");
    m_GetStatus      = (CAN_GetStatus_t)     m_library->resolve("CAN_GetStatus");
    m_Read           = (CAN_Read_t)          m_library->resolve("CAN_Read");
    m_Write          = (CAN_Write_t)         m_library->resolve("CAN_Write");
    m_GetValue       = (CAN_GetValue_t)      m_library->resolve("CAN_GetValue");
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
    m_GetStatus = nullptr;
    m_Read = nullptr;
    m_Write = nullptr;
    m_GetValue = nullptr;
    m_GetErrorText = nullptr;
    m_loaded = false;
}

QList<CanDeviceInfo> PcanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;
    if (!m_loaded || !m_GetValue) return devices;

    // 方式 1: PCANBasic 4.x 的 PCAN_ATTACHED_CHANNELS
    uint32_t channelCount = 0;
    uint32_t res = m_GetValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS_COUNT,
                              &channelCount, sizeof(channelCount));
    if (res == PCAN_ERROR_OK && channelCount > 0 && channelCount <= 64) {
        QVector<TPCANChannelInformation> infoBuf(channelCount);
        res = m_GetValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS,
                         infoBuf.data(), channelCount * sizeof(TPCANChannelInformation));
        if (res == PCAN_ERROR_OK) {
            for (uint32_t i = 0; i < channelCount; ++i) {
                const TPCANChannelInformation &ci = infoBuf[i];
                if (ci.channel_condition == PCAN_CHANNEL_UNAVAILABLE)
                    continue;

                CanDeviceInfo info;
                info.channel = ci.channel_handle;
                info.adapterType = static_cast<int>(CanAdapterType::PCAN);
                info.name = QString::fromLatin1(ci.device_name, MAX_LENGTH_HARDWARE_NAME).trimmed();
                if (info.name.isEmpty())
                    info.name = channelName(ci.channel_handle);

                QString condStr;
                if (ci.channel_condition == PCAN_CHANNEL_AVAILABLE)
                    condStr = QStringLiteral("可用");
                else if (ci.channel_condition == PCAN_CHANNEL_PCANVIEW)
                    condStr = QStringLiteral("可用(PCAN-View)");
                else if (ci.channel_condition == PCAN_CHANNEL_OCCUPIED)
                    condStr = QStringLiteral("被占用");
                info.description = QStringLiteral("%1 [%2]").arg(info.name).arg(condStr);
                devices.append(info);
            }
            return devices;
        }
    }

    // 方式 2: 逐通道查询条件，兼容旧版 PCANBasic
    static const int allChannels[] = {
        PCAN_USBBUS1, PCAN_USBBUS2, PCAN_USBBUS3, PCAN_USBBUS4,
        PCAN_USBBUS5, PCAN_USBBUS6, PCAN_USBBUS7, PCAN_USBBUS8,
        PCAN_USBBUS9, PCAN_USBBUS10,PCAN_USBBUS11,PCAN_USBBUS12,
        PCAN_USBBUS13,PCAN_USBBUS14,PCAN_USBBUS15,PCAN_USBBUS16,
        PCAN_PCIBUS1, PCAN_PCIBUS2, PCAN_PCIBUS3, PCAN_PCIBUS4,
        PCAN_PCIBUS5, PCAN_PCIBUS6, PCAN_PCIBUS7, PCAN_PCIBUS8,
        PCAN_PCIBUS9, PCAN_PCIBUS10,PCAN_PCIBUS11,PCAN_PCIBUS12,
        PCAN_PCIBUS13,PCAN_PCIBUS14,PCAN_PCIBUS15,PCAN_PCIBUS16,
    };

    for (int ch : allChannels) {
        uint16_t handle = (uint16_t)ch;
        uint32_t cond = PCAN_CHANNEL_UNAVAILABLE;
        bool detected = false;
        QString condStr;

        res = m_GetValue(handle, PCAN_CHANNEL_CONDITION, &cond, sizeof(cond));
        if (res == PCAN_ERROR_OK) {
            if (cond == PCAN_CHANNEL_AVAILABLE || cond == PCAN_CHANNEL_PCANVIEW) {
                detected = true;
                condStr = QStringLiteral("可用");
            } else if (cond == PCAN_CHANNEL_OCCUPIED) {
                detected = true;
                condStr = QStringLiteral("被占用");
            }
        }

        // 旧版驱动可能不支持 PCAN_CHANNEL_CONDITION，回退到试初始化
        if (!detected) {
            res = m_Initialize(handle, PCAN_BAUD_500K, 0, 0, 0);
            if (res == PCAN_ERROR_OK) {
                detected = true;
                condStr = QStringLiteral("可用");
                m_Uninitialize(handle);
            }
        }

        if (detected) {
            CanDeviceInfo info;
            info.channel = ch;
            info.adapterType = static_cast<int>(CanAdapterType::PCAN);
            info.name = channelName(ch);
            info.description = QStringLiteral("%1 [%2]").arg(info.name).arg(condStr);
            devices.append(info);
        }
    }

    return devices;
}

bool PcanAdapter::open(int channel, CanBaudRate baud)
{
    if (!m_loaded) return false;
    if (m_opened) close();

    uint32_t res = m_Initialize((TPCANHandle)channel, (TPCANBaudrate)baud, 0, 0, 0);
    if (res != PCAN_ERROR_OK) {
        emit errorOccurred(QString("PCAN 初始化失败: %1").arg(errorText(res)));
        return false;
    }

    m_channel = (uint16_t)channel;
    m_opened = true;

    if (!m_readTimer) {
        m_readTimer = new QTimer(this);
        connect(m_readTimer, &QTimer::timeout, this, &PcanAdapter::onReadTimer);
    }
    m_readTimer->start(1);
    return true;
}

void PcanAdapter::onReadTimer()
{
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
        canMsg.channel = m_channel & 0x0F; // 逻辑通道号
        canMsg.timestamp = QDateTime::currentDateTime();

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

    // PCAN_ERROR_QRCVEMPTY 表示队列已空，属正常退出条件
    if (res != PCAN_ERROR_QRCVEMPTY)
        qWarning() << "PCAN: 读取失败 ch" << m_channel << errorText(res);
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
    // 句柄/驱动/硬件错误说明设备已物理断开；总线错误不影响存活判断
    if (status == PCAN_ERROR_ILLHANDLE ||
        status == PCAN_ERROR_NODRIVER ||
        status == PCAN_ERROR_ILLHW ||
        status == PCAN_ERROR_RESOURCE)
        return false;

    return true;
}

QList<int> PcanAdapter::availableSendChannels() const
{
    QList<int> channels;
    if (m_opened)
        channels.append(m_channel & 0x0F);
    return channels;
}

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
