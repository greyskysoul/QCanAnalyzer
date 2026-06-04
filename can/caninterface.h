#ifndef CANINTERFACE_H
#define CANINTERFACE_H

#include "canmessage.h"
#include <QObject>
#include <QString>
#include <QList>

/// CAN 设备信息
struct CanDeviceInfo {
    QString name;        // 显示名称
    QString description; // 详细描述
    int channel = -1;    // 通道号
    int adapterType = 0; // 适配器类型: 0=PCAN, 1=gs_usb, 2=SocketCAN
};

/// 适配器类型
enum class CanAdapterType {
    PCAN = 0,
    GsUsb,
    SocketCAN
};

/// 波特率预设
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

/// 波特率转字符串
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

/// 字符串转波特率
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

/// CAN 接口抽象基类 —— 所有 CAN 适配器必须实现此接口
class CanInterface : public QObject
{
    Q_OBJECT

public:
    explicit CanInterface(QObject *parent = nullptr) : QObject(parent) {}
    ~CanInterface() override = default;

    /// 扫描可用设备
    virtual QList<CanDeviceInfo> scanDevices() = 0;

    /// 打开指定通道
    virtual bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) = 0;

    /// 关闭当前通道
    virtual void close() = 0;

    /// 是否已打开
    virtual bool isOpen() const = 0;

    /// 发送一条 CAN 消息 (error 返回 false)
    virtual bool sendMessage(const CanMessage &msg) = 0;

    /// 适配器类型名称
    virtual QString adapterName() const = 0;

    /// 检查当前连接是否存活 (true=正常, false=已断开)
    virtual bool isAlive() const { return isOpen(); }

signals:
    /// 收到 CAN 消息
    void messageReceived(const CanMessage &msg);

    /// 错误发生
    void errorOccurred(const QString &error);
};

#endif // CANINTERFACE_H
