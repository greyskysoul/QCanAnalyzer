#ifndef MOCKCANADAPTER_H
#define MOCKCANADAPTER_H

#include "can/caninterface.h"
#include <QTimer>
#include <QRandomGenerator>

/// 虚拟 CAN 适配器 —— 仅在 Debug 模式下编译，用于无硬件时测试 UI
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
    QString adapterName() const override { return tr("MockCAN (虚拟)"); }
    QList<int> availableSendChannels() const override;

private slots:
    void onRxTick();

private:
    CanMessage generateRandomMessage(int channel);

    bool      m_opened = false;
    QTimer   *m_rxTimer = nullptr;
    uint32_t  m_msgCounter = 0; // 用于生成有规律变化的数据
};

#endif // MOCKCANADAPTER_H
