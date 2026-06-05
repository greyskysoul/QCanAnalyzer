#ifndef CANMESSAGE_H
#define CANMESSAGE_H

#include <QString>
#include <QDateTime>
#include <cstdint>

/// CAN 消息类型
enum class CanFrameType {
    StandardData,   // 标准数据帧
    ExtendedData,   // 扩展数据帧
    Remote,         // 远程帧
    Error,          // 错误帧
    Status          // 状态帧
};

/// CAN 消息方向
enum class CanDirection {
    Rx,  // 接收
    Tx   // 发送
};

/// 单条 CAN 消息
struct CanMessage {
    uint32_t id = 0;               // CAN ID
    CanFrameType type = CanFrameType::StandardData;
    CanDirection direction = CanDirection::Rx;
    uint8_t dlc = 0;               // 数据长度 (0~64, CAN FD 最大 64)
    uint8_t data[64] = {};         // 数据 (CAN FD 支持 64 字节)
    int channel = 0;               // 通道号（多通道设备区分用）
    bool isFd = false;             // 是否为 CAN FD 帧
    QDateTime timestamp;           // 时间戳
    uint32_t cycleTimeUs = 0;      // 与上一条同ID消息的周期(微秒), 0=未知

    QString typeString() const {
        QString s;
        switch (type) {
        case CanFrameType::StandardData: s = "DATA"; break;
        case CanFrameType::ExtendedData: s = "EXTD"; break;
        case CanFrameType::Remote:       s = "REMT"; break;
        case CanFrameType::Error:        s = "ERR";  break;
        case CanFrameType::Status:       s = "STAT"; break;
        default: return "?";
        }
        if (isFd)
            s += "(FD)";
        return s;
    }

    QString idString() const {
        int width = (type == CanFrameType::ExtendedData) ? 8 : 3;
        return "0x" + QString("%1").arg(id, width, 16, QChar('0')).toUpper();
    }

    QString dataHex() const {
        if (dlc == 0) return {};
        QString s;
        for (int i = 0; i < dlc && i < 64; ++i)
            s += QString("%1 ").arg(data[i], 2, 16, QChar('0')).toUpper();
        return s.trimmed();
    }
};

/// CAN FD DLC 编码 → 实际字节数
/// DLC  0~8  →  0~8
/// DLC  9    → 12
/// DLC 10    → 16
/// DLC 11    → 20
/// DLC 12    → 24
/// DLC 13    → 32
/// DLC 14    → 48
/// DLC 15    → 64
inline int canFdDlcToLen(uint8_t dlc)
{
    static const uint8_t map[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return (dlc < 16) ? map[dlc] : 64;
}

#endif // CANMESSAGE_H
