#ifndef CANINTERFACE_H
#define CANINTERFACE_H

#include "canmessage.h"
#include <QObject>
#include <QString>
#include <QList>

struct CanDeviceInfo {
    QString name;
    QString description;
    int channel = -1;    // 高字节=设备索引, 低字节=通道号 (PCAN 例外: 16 位硬件 handle)
    int adapterType = 0; // CanAdapterType 的整数值
    int deviceType = 0;  // 厂商 SDK 的设备类型代码
    int deviceIndex = 0;
    int channelCount = 1;
};

/// 数值被持久化为 int，新增项只能追加在 MockCan 之前
enum class CanAdapterType {
    PCAN = 0,
    GsUsb,
    SocketCAN,
    ZCANFD,
    ZCAN,
#ifdef QT_DEBUG
    MockCan
#endif
};

/// 数值即 PCANBasic 的 BTR0BTR1 编码
enum class CanBaudRate {
    BR_1M   = 0x0014,
    BR_800K = 0x0016,
    BR_500K = 0x001C,
    BR_250K = 0x011C,
    BR_125K = 0x031C,
    BR_100K = 0x432F,
    BR_50K  = 0x472F,
    BR_20K  = 0x532F,
    BR_10K  = 0x672F,
    BR_5K   = 0x7F7F
};

inline QString baudRateString(CanBaudRate br) {
    switch (br) {
    case CanBaudRate::BR_1M:   return "1M";
    case CanBaudRate::BR_800K: return "800K";
    case CanBaudRate::BR_500K: return "500K";
    case CanBaudRate::BR_250K: return "250K";
    case CanBaudRate::BR_125K: return "125K";
    case CanBaudRate::BR_100K: return "100K";
    case CanBaudRate::BR_50K:  return "50K";
    case CanBaudRate::BR_20K:  return "20K";
    case CanBaudRate::BR_10K:  return "10K";
    case CanBaudRate::BR_5K:   return "5K";
    default: return "?";
    }
}

inline CanBaudRate baudRateFromString(const QString &str) {
    if (str == "1M")    return CanBaudRate::BR_1M;
    if (str == "800K")  return CanBaudRate::BR_800K;
    if (str == "500K")  return CanBaudRate::BR_500K;
    if (str == "250K")  return CanBaudRate::BR_250K;
    if (str == "125K")  return CanBaudRate::BR_125K;
    if (str == "100K")  return CanBaudRate::BR_100K;
    if (str == "50K")   return CanBaudRate::BR_50K;
    if (str == "20K")   return CanBaudRate::BR_20K;
    if (str == "10K")   return CanBaudRate::BR_10K;
    if (str == "5K")    return CanBaudRate::BR_5K;
    return CanBaudRate::BR_500K;
}

/// CAN 适配器抽象基类。所有实现运行在 GUI 线程，无需考虑并发。
class CanInterface : public QObject
{
    Q_OBJECT

public:
    explicit CanInterface(QObject *parent = nullptr) : QObject(parent) {}
    ~CanInterface() override = default;

    virtual QList<CanDeviceInfo> scanDevices() = 0;
    virtual bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool sendMessage(const CanMessage &msg) = 0;
    virtual QString adapterName() const = 0;

    /// 覆写以做真实硬件探测；默认只反映 open/close 状态，无法发现物理拔出
    virtual bool isAlive() const { return isOpen(); }

    virtual QList<int> availableSendChannels() const { return {}; }
    virtual bool setSendChannel(int channel) { Q_UNUSED(channel); return false; }
    virtual int currentSendChannel() const { return -1; }

signals:
    void messageReceived(const CanMessage &msg);
    void errorOccurred(const QString &error);
};

#endif // CANINTERFACE_H
