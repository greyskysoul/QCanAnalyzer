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
    QDateTime timestamp;           // 时间戳
    uint32_t cycleTimeUs = 0;      // 与上一条同ID消息的周期(微秒), 0=未知

    QString typeString() const {
        switch (type) {
        case CanFrameType::StandardData: return "DATA";
        case CanFrameType::ExtendedData: return "EXTD";
        case CanFrameType::Remote:       return "REMT";
        case CanFrameType::Error:        return "ERR";
        case CanFrameType::Status:       return "STAT";
        }
        return "?";
    }

    QString idString() const {
        if (type == CanFrameType::ExtendedData)
            return QString("0x%1").arg(id, 8, 16, QChar('0')).toUpper();
        return QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper();
    }

    QString dataHex() const {
        if (dlc == 0) return {};
        QString s;
        for (int i = 0; i < dlc && i < 64; ++i)
            s += QString("%1 ").arg(data[i], 2, 16, QChar('0')).toUpper();
        return s.trimmed();
    }
};

#endif // CANMESSAGE_H
