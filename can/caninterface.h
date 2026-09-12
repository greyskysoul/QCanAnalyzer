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

/// CAN-FD 数据域波特率，数值即 Hz。
/// 经典 CAN 没有数据域，因此 None 同时充当 CAN-FD 的开关。
enum class CanDataBaudRate {
    None    = 0,
    BR_500K = 500000,
    BR_1M   = 1000000,
    BR_2M   = 2000000,
    BR_4M   = 4000000,
    BR_5M   = 5000000,
    BR_8M   = 8000000,
    BR_10M  = 10000000
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

/// 仲裁域波特率 (Hz)，供按 Hz 配置的 SDK 使用
inline uint32_t baudRateHz(CanBaudRate br) {
    switch (br) {
    case CanBaudRate::BR_1M:   return 1000000;
    case CanBaudRate::BR_800K: return 800000;
    case CanBaudRate::BR_500K: return 500000;
    case CanBaudRate::BR_250K: return 250000;
    case CanBaudRate::BR_125K: return 125000;
    case CanBaudRate::BR_100K: return 100000;
    case CanBaudRate::BR_50K:  return 50000;
    case CanBaudRate::BR_20K:  return 20000;
    case CanBaudRate::BR_10K:  return 10000;
    case CanBaudRate::BR_5K:   return 5000;
    }
    return 500000;
}

/// 数据域波特率 (Hz)
inline uint32_t dataBaudRateHz(CanDataBaudRate br) {
    return static_cast<uint32_t>(br);
}

/// None 返回空串
inline QString dataBaudRateString(CanDataBaudRate br) {
    switch (br) {
    case CanDataBaudRate::None:    return QString();
    case CanDataBaudRate::BR_500K: return "500K";
    case CanDataBaudRate::BR_1M:   return "1M";
    case CanDataBaudRate::BR_2M:   return "2M";
    case CanDataBaudRate::BR_4M:   return "4M";
    case CanDataBaudRate::BR_5M:   return "5M";
    case CanDataBaudRate::BR_8M:   return "8M";
    case CanDataBaudRate::BR_10M:  return "10M";
    }
    return QString();
}

/// 未识别时回退到 BR_2M（CAN-FD 最常见的默认值）；None 需显式传入
inline CanDataBaudRate dataBaudRateFromString(const QString &str) {
    if (str.isEmpty()) return CanDataBaudRate::None;
    if (str == "500K") return CanDataBaudRate::BR_500K;
    if (str == "1M")   return CanDataBaudRate::BR_1M;
    if (str == "2M")   return CanDataBaudRate::BR_2M;
    if (str == "4M")   return CanDataBaudRate::BR_4M;
    if (str == "5M")   return CanDataBaudRate::BR_5M;
    if (str == "8M")   return CanDataBaudRate::BR_8M;
    if (str == "10M")  return CanDataBaudRate::BR_10M;
    return CanDataBaudRate::BR_2M;
}

/// UI 中可选的数据域波特率（不含 None）——新增取值只需改这里
inline QList<CanDataBaudRate> selectableDataBaudRates() {
    return { CanDataBaudRate::BR_2M, CanDataBaudRate::BR_4M, CanDataBaudRate::BR_5M,
             CanDataBaudRate::BR_8M, CanDataBaudRate::BR_10M };
}

/// CAN 适配器抽象基类。所有实现运行在 GUI 线程，无需考虑并发。
class CanInterface : public QObject
{
    Q_OBJECT

public:
    explicit CanInterface(QObject *parent = nullptr) : QObject(parent) {}
    ~CanInterface() override = default;

    virtual QList<CanDeviceInfo> scanDevices() = 0;

    /// @param baud     仲裁域波特率
    /// @param dataBaud 数据域波特率；None 表示经典 CAN（不使用数据域）
    virtual bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K,
                      CanDataBaudRate dataBaud = CanDataBaudRate::None) = 0;

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
