#ifndef SOCKETCANADAPTER_H
#define SOCKETCANADAPTER_H

#include "can/caninterface.h"

class QSocketNotifier;

/// SocketCAN 适配器 (Linux 内核 CAN 子系统)
class SocketCanAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit SocketCanAdapter(QObject *parent = nullptr);
    ~SocketCanAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    /// @param ifName SocketCAN 接口名 (如 "can0", "vcan0")
    bool open(const QString &ifName);
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "SocketCAN"; }

    static QString channelName(int channel);

    /// SocketCAN 不支持软件设置波特率
    bool supportsBaudConfig() const { return false; }

private slots:
    /// QSocketNotifier 激活时读取 CAN 帧
    void readSocket();

private:
    int              m_socketFd = -1;
    bool             m_opened = false;
    QString          m_ifName;              // 当前打开的接口名
    QSocketNotifier *m_notifier = nullptr;  // 监听 socket fd 可读事件
};

#endif // SOCKETCANADAPTER_H
