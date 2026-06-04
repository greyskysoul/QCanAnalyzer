#include "socketcanadapter.h"
#include <QDebug>
#include <QTimer>
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
