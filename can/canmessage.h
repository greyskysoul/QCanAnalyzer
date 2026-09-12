#ifndef CANMESSAGE_H
#define CANMESSAGE_H

#include <QString>
#include <QDateTime>
#include <QtGlobal>
#include <cstdint>

enum class CanFrameType { StandardData, ExtendedData, Remote, Error, Status };
enum class CanDirection { Rx, Tx };

struct CanMessage {
    uint32_t id = 0;
    CanFrameType type = CanFrameType::StandardData;
    CanDirection direction = CanDirection::Rx;
    uint8_t dlc = 0;               // 数据字节数 (0~64)；适配器需在收/发时与 SDK 的 DLC 编码互转
    uint8_t data[64] = {};
    int channel = 0;
    bool isFd = false;
    QDateTime timestamp;

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

/// CAN FD DLC 编码 (0~15) → 实际字节数 (9~15 对应 12/16/20/24/32/48/64)
inline int canFdDlcToLen(uint8_t dlc)
{
    static const uint8_t map[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return (dlc < 16) ? map[dlc] : 64;
}

/// 实际字节数 → CAN FD DLC 编码，供需要提交 DLC 而非字节数的 API 使用
inline uint8_t canFdLenToDlc(int len)
{
    if (len <= 8) return static_cast<uint8_t>(qMax(len, 0));
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

/// 把任意长度对齐到 CAN FD 合法字节数 (0~8/12/16/20/24/32/48/64)
inline int canFdSnapLen(int len)
{
    return canFdDlcToLen(canFdLenToDlc(len));
}

#endif // CANMESSAGE_H
