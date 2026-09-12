#include "socketcanadapter.h"
#include <QDebug>
#include <QSocketNotifier>
#include <QDateTime>
#include <QDir>

#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#endif

SocketCanAdapter::SocketCanAdapter(QObject *parent)
    : CanInterface(parent)
{
}

SocketCanAdapter::~SocketCanAdapter()
{
    close();
}

void SocketCanAdapter::readSocket()
{
#ifdef Q_OS_LINUX
    if (m_socketFd < 0) return;

    // canfd_frame 同时覆盖经典帧与 FD 帧，由 read() 返回的字节数区分
    struct canfd_frame frame;
    // 限流：避免高负载下长时间占用事件循环
    for (int i = 0; i < 64; ++i) {
        ssize_t n = ::read(m_socketFd, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            emit errorOccurred(QString("SocketCAN: 读取错误 (%1)").arg(strerror(errno)));
            break;
        }
        if (n != CAN_MTU && n != CANFD_MTU) {
            qWarning() << "SocketCAN: 收到异常帧大小" << n;
            break;
        }

        const bool isFd = (n == CANFD_MTU);
        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = 0;
        msg.timestamp = QDateTime::currentDateTime();
        msg.isFd = isFd;

        if (frame.can_id & CAN_EFF_FLAG) {
            msg.type = CanFrameType::ExtendedData;
            msg.id = frame.can_id & CAN_EFF_MASK;
        } else {
            msg.type = CanFrameType::StandardData;
            msg.id = frame.can_id & CAN_SFF_MASK;
        }
        if (frame.can_id & CAN_RTR_FLAG)
            msg.type = CanFrameType::Remote;
        if (frame.can_id & CAN_ERR_FLAG)
            msg.type = CanFrameType::Error;

        // FD 帧的 len 是 DLC 编码，经典帧的 can_dlc 是字节数
        const uint8_t dlc = frame.len & 0x0F;
        msg.dlc = isFd ? static_cast<uint8_t>(canFdDlcToLen(dlc))
                       : (dlc > 8 ? 8 : dlc);
        for (int j = 0; j < msg.dlc && j < 64; ++j)
            msg.data[j] = frame.data[j];

        emit messageReceived(msg);
    }
#endif
}

QList<CanDeviceInfo> SocketCanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;
#ifdef Q_OS_LINUX
    QDir netDir("/sys/class/net");
    const QStringList filters = {"can*", "vcan*"};
    for (const auto &ifName : netDir.entryList(filters, QDir::Dirs | QDir::NoDotAndDotDot)) {
        CanDeviceInfo info;
        info.name = ifName;
        info.channel = devices.size(); // channel 即接口在列表中的下标
        info.adapterType = static_cast<int>(CanAdapterType::SocketCAN);
        info.description = QString("SocketCAN: %1").arg(ifName);
        devices.append(info);
    }
#endif
    return devices;
}

bool SocketCanAdapter::open(const QString &ifName)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(ifName);
    emit errorOccurred("SocketCAN 仅支持 Linux");
    return false;
#else
    if (m_opened) close();

    int sock = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        emit errorOccurred(QString("SocketCAN: 创建 socket 失败 (%1)").arg(strerror(errno)));
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName.toLatin1().constData(), IFNAMSIZ - 1);
    if (::ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        emit errorOccurred(QString("SocketCAN: 接口 %1 不存在").arg(ifName));
        ::close(sock);
        return false;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        emit errorOccurred(QString("SocketCAN: 绑定 %1 失败 (%2)").arg(ifName).arg(strerror(errno)));
        ::close(sock);
        return false;
    }

    m_socketFd = sock;
    m_opened = true;

    // 非阻塞，避免 readSocket() 阻塞事件循环
    int flags = ::fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(sock, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &SocketCanAdapter::readSocket);

    return true;
#endif
}

bool SocketCanAdapter::open(int channel, CanBaudRate baud, CanDataBaudRate dataBaud)
{
    // 波特率（含 FD 数据域）由内核按 `ip link` 配置决定，此处不接收
    Q_UNUSED(baud);
    Q_UNUSED(dataBaud);

#ifndef Q_OS_LINUX
    Q_UNUSED(channel);
    emit errorOccurred("SocketCAN 仅支持 Linux");
    return false;
#else
    // channel 是 scanDevices() 返回列表中的下标
    const QList<CanDeviceInfo> devices = scanDevices();
    if (channel >= 0 && channel < devices.size())
        return open(devices[channel].name);
    return devices.isEmpty() ? false : open(devices.first().name);
#endif
}

void SocketCanAdapter::close()
{
#ifdef Q_OS_LINUX
    // notifier 必须在关闭 fd 之前销毁
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
#endif
    m_opened = false;
}

bool SocketCanAdapter::isOpen() const
{
    return m_opened;
}

bool SocketCanAdapter::sendMessage(const CanMessage &msg)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(msg);
    return false;
#else
    if (m_socketFd < 0) return false;

    if (msg.isFd) {
        struct canfd_frame frame;
        memset(&frame, 0, sizeof(frame));
        frame.can_id = msg.id;
        if (msg.type == CanFrameType::ExtendedData)
            frame.can_id |= CAN_EFF_FLAG;
        if (msg.type == CanFrameType::Remote)
            frame.can_id |= CAN_RTR_FLAG;
        frame.len = canFdLenToDlc(msg.dlc);
        const int copyLen = canFdSnapLen(msg.dlc);
        for (int i = 0; i < copyLen; ++i)
            frame.data[i] = msg.data[i];

        return ::write(m_socketFd, &frame, sizeof(frame)) == (ssize_t)sizeof(frame);
    }

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = msg.id;
    if (msg.type == CanFrameType::ExtendedData)
        frame.can_id |= CAN_EFF_FLAG;
    if (msg.type == CanFrameType::Remote)
        frame.can_id |= CAN_RTR_FLAG;
    frame.can_dlc = msg.dlc > 8 ? 8 : msg.dlc;
    for (int i = 0; i < frame.can_dlc; ++i)
        frame.data[i] = msg.data[i];

    return ::write(m_socketFd, &frame, sizeof(frame)) == (ssize_t)sizeof(frame);
#endif
}

bool SocketCanAdapter::isAlive() const
{
    return m_opened;
}

QString SocketCanAdapter::channelName(int channel)
{
    Q_UNUSED(channel);
    return "SocketCAN";
}
