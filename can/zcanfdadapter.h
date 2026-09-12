#ifndef ZCANFDADAPTER_H
#define ZCANFDADAPTER_H

#include "can/caninterface.h"
#include "controlcanfd.h"
#include <QSet>
#include <QTimer>

/// ZCANFD 适配器 (ZLG USBCANFD 系列设备)
/// 直接调用 ControlCANFD 静态 API (Windows 导入库 / Linux libControlCANFD.a)
class ZcanFdAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit ZcanFdAdapter(QObject *parent = nullptr);
    ~ZcanFdAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "ZCANFD"; }

    QList<int> availableSendChannels() const override;
    bool setSendChannel(int channel) override;
    int currentSendChannel() const override;

    static QString channelName(int channel);

private:
    void onReadTimer();
    void pollMessages();
    UINT baudToHz(CanBaudRate baud) const;

    DEVICE_HANDLE   m_devHandle = nullptr;
    UINT            m_deviceType = USBCANFD_200U;
    UINT            m_deviceIndex = 0;
    UINT            m_canIndex = 0;
    int             m_totalChannels = 1;
    bool            m_opened = false;

    struct ChannelInfo {
        CHANNEL_HANDLE handle = nullptr;
        UINT chIdx = 0;
    };
    QList<ChannelInfo> m_openChannels;

    QTimer *m_readTimer = nullptr;

    // 重新扫描会对已占用设备调 ZCAN_OpenDevice 导致发送失败，故占用期间禁用扫描
    static int s_openCount;
    static QSet<UINT> s_openDeviceIndices;
    static QList<CanDeviceInfo> s_cachedDevices;
};

#endif // ZCANFDADAPTER_H
