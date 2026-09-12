#ifndef SOCKETCANADAPTER_H
#define SOCKETCANADAPTER_H

#include "can/caninterface.h"

class QSocketNotifier;

/// SocketCAN 适配器 (Linux 内核 CAN 子系统)
/// 不支持软件设置波特率，需预先用 `ip link` 配置接口
class SocketCanAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit SocketCanAdapter(QObject *parent = nullptr);
    ~SocketCanAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    /// @param ifName 接口名 (如 "can0", "vcan0")
    bool open(const QString &ifName);
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K,
              CanDataBaudRate dataBaud = CanDataBaudRate::None) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "SocketCAN"; }

    static QString channelName(int channel);

private slots:
    void readSocket();

private:
    int              m_socketFd = -1;
    bool             m_opened = false;
    QSocketNotifier *m_notifier = nullptr;
};

#endif // SOCKETCANADAPTER_H
