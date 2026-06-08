#ifndef ZCANFDADAPTER_H
#define ZCANFDADAPTER_H

#include "can/caninterface.h"
#include "controlcanfd.h"
#include <QSet>
#include <QTimer>

/// ZCANFD 适配器 (ZLG USBCANFD 系列设备)
/// Windows: 动态加载 ControlCANFD.dll
/// Linux:   静态链接 libcontrolcanfd.a
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
    QString errorText(UINT err);
    UINT baudToHz(CanBaudRate baud) const;

    // ─── 设备句柄 ───
    DEVICE_HANDLE   m_devHandle = nullptr;
    UINT            m_deviceType = USBCANFD_200U;
    UINT            m_deviceIndex = 0;
    UINT            m_canIndex = 0;
    int             m_totalChannels = 1;
    bool            m_opened = false;
    bool            m_isFdSupported = true;

    struct ChannelInfo {
        CHANNEL_HANDLE handle = nullptr;
        UINT chIdx = 0;
        CanBaudRate baud = CanBaudRate::BR_500K;
    };
    QList<ChannelInfo> m_openChannels;

    // ─── 读取轮询 ───
    QTimer *m_readTimer = nullptr;

    // 静态缓存: 打开后禁止扫描 (扫描会 OpenDevice 已占用的设备, 导致发送失败)
    static int s_openCount;
    static QSet<UINT> s_openDeviceIndices;  // 已打开的设备索引集合, 防止重复打开
    static QList<CanDeviceInfo> s_cachedDevices;
};

#endif // ZCANFDADAPTER_H
