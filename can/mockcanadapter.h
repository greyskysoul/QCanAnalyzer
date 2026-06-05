#ifndef MOCKCANADAPTER_H
#define MOCKCANADAPTER_H

#include "can/caninterface.h"
#include <QTimer>
#include <QRandomGenerator>

/// 虚拟 CAN 适配器 —— 仅在 Debug 模式下使用，用于无硬件时测试 UI
/// 支持模拟 CAN 报文收发，以及随机错误注入
class MockCanAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit MockCanAdapter(QObject *parent = nullptr);
    ~MockCanAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "MockCAN (虚拟)"; }

    /// 设置随机接收报文的频率（毫秒），0=不自动接收
    void setAutoRxInterval(int ms);

    /// 设置是否模拟偶尔的发送失败
    void setSimulateError(bool enable) { m_simulateError = enable; }

private slots:
    void onRxTick();

private:
    /// 生成一条随机 CAN 报文
    CanMessage generateRandomMessage();

    bool      m_opened = false;
    int       m_channel = 0;
    QTimer   *m_rxTimer = nullptr;
    bool      m_simulateError = false;
    uint32_t  m_msgCounter = 0;    // 报文计数器，用于生成变化的数据
};

#endif // MOCKCANADAPTER_H
