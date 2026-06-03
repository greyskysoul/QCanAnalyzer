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

bool SocketCanAdapter::open(int channel, CanBaudRate baud)
{
    Q_UNUSED(channel);
    Q_UNUSED(baud);

#ifndef Q_OS_LINUX
    emit errorOccurred("SocketCAN 仅支持 Linux");
    return false;
#else
    // SocketCAN 需要通过接口名打开（channel 参数在此不做映射）
    // 实际使用时由调用者确保接口名正确
    // 这里返回 true 作为占位——真正的 SocketCAN 需要知道接口名
    m_opened = true;
    return true;
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
