#ifndef ZCANADAPTER_H
#define ZCANADAPTER_H

#include "can/caninterface.h"
#include <QSet>
#include <QTimer>

/// ZCAN 适配器 (ZLG USBCAN 系列, VCI API) — 静态链接 ControlCAN.lib
class ZcanAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit ZcanAdapter(QObject *parent = nullptr);
    ~ZcanAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "ZCAN"; }

    QList<int> availableSendChannels() const override;
    bool setSendChannel(int channel) override;
    int currentSendChannel() const override;

    static QString channelName(int channel);

private:
    void onReadTimer();
    void pollMessages();
    unsigned int timing0ForBaud(CanBaudRate baud) const;
    unsigned int timing1ForBaud(CanBaudRate baud) const;

    unsigned long m_deviceType = 4;   // VCI_USBCAN2
    unsigned long m_deviceIndex = 0;
    unsigned long m_canIndex = 0;
    int           m_totalChannels = 1;
    bool          m_opened = false;

    struct ChannelInfo {
        unsigned long chIdx = 0;
        CanBaudRate baud = CanBaudRate::BR_500K;
    };
    QList<ChannelInfo> m_openChannels;

    QTimer *m_readTimer = nullptr;

    // 静态缓存: 引用计数, 所有设备关闭后才允许重新扫描
    static int s_openCount;
    static QSet<unsigned long> s_openDeviceIndices;  // 已打开的设备索引集合, 防止重复打开
    static QList<CanDeviceInfo> s_cachedDevices;
};

#endif // ZCANADAPTER_H
