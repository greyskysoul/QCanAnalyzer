#ifndef GSUSBADAPTER_H
#define GSUSBADAPTER_H

#include "can/caninterface.h"

class QTimer;

/// gs_usb CAN 适配器 (candleLight / gs_usb 开源固件设备)
/// 使用 candle API (来自 cangaroo 项目) 进行静态链接
class GsUsbAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit GsUsbAdapter(QObject *parent = nullptr);
    ~GsUsbAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "gs_usb"; }

    static QString channelName(int channel);

private slots:
    void onReadTimer();

private:
    void recoverChannel(); // 尝试从 Bus-Off / 错误状态恢复通道

    // candle API 使用不透明指针
    void *m_devHandle = nullptr;  // candle_handle
    void *m_devList = nullptr;    // candle_list_handle
    int    m_channelCount = 0;
    uint8_t m_channelIndex = 0;   // 当前打开的通道号
    bool   m_opened = false;
    bool   m_deviceLost = false;  // 设备是否已物理断开
    QTimer *m_readTimer = nullptr; // 读取轮询定时器

    // Bus-Off / 错误恢复状态
    int    m_errorFrameCount = 0;     // 连续错误帧计数
    int    m_maxErrorBeforeRecover = 50; // 超过此值尝试恢复
    bool   m_recovering = false;      // 正在恢复中
    int    m_recoverAttempt = 0;      // 恢复尝试次数
    int    m_maxRecoverAttempts = 5;  // 最大恢复尝试次数
};

#endif // GSUSBADAPTER_H
