#include "socketcanadapter.h"
#include <QDebug>
#include <QSocketNotifier>
#include <QDateTime>
#include <QDir>
#include <QMessageBox>

#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
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

    struct can_frame frame;
    // 限制单次读取帧数，避免在高负载下长时间占用事件循环
    for (int i = 0; i < 64; ++i) {
        ssize_t n = ::read(m_socketFd, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // 没有更多数据
            emit errorOccurred(QString("SocketCAN: 读取错误 (%1)").arg(strerror(errno)));
            break;
        }
        if (n != CAN_MTU && n != CANFD_MTU) {
            qWarning() << "SocketCAN: 收到异常帧大小" << n;
            break;
        }

        CanMessage msg;
        msg.direction = CanDirection::Rx;
        msg.channel = m_channel;
        msg.timestamp = QDateTime::currentDateTime();

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

        msg.dlc = frame.can_dlc > 8 ? 8 : frame.can_dlc;
        for (int i = 0; i < msg.dlc && i < 8; ++i)
            msg.data[i] = frame.data[i];

        emit messageReceived(msg);
    }
#endif
}

QList<CanDeviceInfo> SocketCanAdapter::scanDevices()
{
    QList<CanDeviceInfo> devices;
#ifndef Q_OS_LINUX
    Q_UNUSED(this);
    return devices;
#else
    // 扫描 /sys/class/net/ 下所有 can* 网络接口
    QDir netDir("/sys/class/net");
    QStringList filters;
    filters << "can*" << "vcan*";
    for (const auto &ifName : netDir.entryList(filters, QDir::Dirs | QDir::NoDotAndDotDot)) {
        CanDeviceInfo info;
        info.name = ifName;
        info.channel = 0; // SocketCAN 用接口名作为通道
        info.adapterType = static_cast<int>(CanAdapterType::SocketCAN);
        info.description = QString("SocketCAN: %1").arg(ifName);
        devices.append(info);
    }
    return devices;
#endif
}

bool SocketCanAdapter::open(const QString &ifName)
{
    Q_UNUSED(ifName);
#ifndef Q_OS_LINUX
    emit errorOccurred("SocketCAN 仅支持 Linux");
    return false;
#else
    if (m_opened) close();

    // 创建 CAN_RAW socket
    int sock = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        emit errorOccurred(QString("SocketCAN: 创建 socket 失败 (%1)").arg(strerror(errno)));
        return false;
    }

    // 绑定到指定接口
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
    m_ifName = ifName;
    m_opened = true;

    // 设置非阻塞模式，避免 readSocket() 阻塞事件循环
    int flags = ::fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // 使用 QSocketNotifier 异步监听 CAN 帧
    m_notifier = new QSocketNotifier(sock, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &SocketCanAdapter::readSocket);

    return true;
#endif
}

bool SocketCanAdapter::open(int channel, CanBaudRate baud)
{
    Q_UNUSED(channel);
    Q_UNUSED(baud);

#ifndef Q_OS_LINUX
    emit errorOccurred("SocketCAN 仅支持 Linux");
    return false;
#else
    // 从扫描结果中获取接口名 (channel 参数在此设计中不直接使用)
    // 默认尝试 "can0"
    QList<CanDeviceInfo> devices = scanDevices();
    if (!devices.isEmpty())
        return open(devices.first().name);
    return open(QString("can0"));
#endif
}

void SocketCanAdapter::close()
{
#ifdef Q_OS_LINUX
    // 先删除 notifier（必须在 close fd 之前）
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

    int nbytes = write(m_socketFd, &frame, sizeof(frame));
    return nbytes == sizeof(frame);
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
