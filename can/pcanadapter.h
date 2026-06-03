#ifndef PCANADAPTER_H
#define PCANADAPTER_H

#include "can/caninterface.h"
#include <QLibrary>

// ─── PCAN Basic API 常量 (与 PCANBasic.h 保持一致) ───

#define PCAN_CHANNEL_AVAILABLE    0x01
#define PCAN_CHANNEL_OCCUPIED     0x02

#define PCAN_ERROR_OK             0x00000
#define PCAN_ERROR_XMTFULL        0x00001
#define PCAN_ERROR_OVERRUN        0x00002
#define PCAN_ERROR_BUSLIGHT       0x00004
#define PCAN_ERROR_BUSHEAVY       0x00008
#define PCAN_ERROR_BUSOFF         0x00010
#define PCAN_ERROR_ANYBUSERR      (PCAN_ERROR_BUSLIGHT | PCAN_ERROR_BUSHEAVY | PCAN_ERROR_BUSOFF)
#define PCAN_ERROR_QRCVEMPTY      0x00020
#define PCAN_ERROR_QOVERRUN       0x00040
#define PCAN_ERROR_QXMTFULL       0x00080
#define PCAN_ERROR_REGTEST        0x00100
#define PCAN_ERROR_NODRIVER       0x00200
#define PCAN_ERROR_HWINUSE        0x00400
#define PCAN_ERROR_NETINUSE       0x00800
#define PCAN_ERROR_ILLHW          0x01400
#define PCAN_ERROR_ILLNET         0x01800
#define PCAN_ERROR_ILLCLIENT      0x01C00
#define PCAN_ERROR_ILLHANDLE      0x02000
#define PCAN_ERROR_RESOURCE       0x04000
#define PCAN_ERROR_ILLPARAMTYPE   0x08000
#define PCAN_ERROR_ILLPARAMVAL    0x10000
#define PCAN_ERROR_UNKNOWN        0x100000
#define PCAN_ERROR_ILLDATA        0x200000
#define PCAN_ERROR_INITIALIZE     0x400000

#define PCAN_NONEBUS              0x00
#define PCAN_ISABUS1              0x21
#define PCAN_ISABUS2              0x22
#define PCAN_ISABUS3              0x23
#define PCAN_ISABUS4              0x24
#define PCAN_ISABUS5              0x25
#define PCAN_ISABUS6              0x26
#define PCAN_ISABUS7              0x27
#define PCAN_ISABUS8              0x28
#define PCAN_DNGBUS1              0x31
#define PCAN_PCIBUS1              0x41
#define PCAN_PCIBUS2              0x42
#define PCAN_PCIBUS3              0x43
#define PCAN_PCIBUS4              0x44
#define PCAN_PCIBUS5              0x45
#define PCAN_PCIBUS6              0x46
#define PCAN_PCIBUS7              0x47
#define PCAN_PCIBUS8              0x48
#define PCAN_USBBUS1              0x51
#define PCAN_USBBUS2              0x52
#define PCAN_USBBUS3              0x53
#define PCAN_USBBUS4              0x54
#define PCAN_USBBUS5              0x55
#define PCAN_USBBUS6              0x56
#define PCAN_USBBUS7              0x57
#define PCAN_USBBUS8              0x58
#define PCAN_USBBUS9              0x509
#define PCAN_USBBUS10             0x50A
#define PCAN_USBBUS11             0x50B
#define PCAN_USBBUS12             0x50C
#define PCAN_USBBUS13             0x50D
#define PCAN_USBBUS14             0x50E
#define PCAN_USBBUS15             0x50F
#define PCAN_USBBUS16             0x510

#define PCAN_MESSAGE_STANDARD     0x00
#define PCAN_MESSAGE_RTR          0x01
#define PCAN_MESSAGE_EXTENDED     0x02
#define PCAN_MESSAGE_STATUS       0x80

#define PCAN_TYPE_ISA             0x01
#define PCAN_TYPE_ISA_SJA         0x09
#define PCAN_TYPE_ISA_PHYTEC      0x04
#define PCAN_TYPE_DNG             0x02
#define PCAN_TYPE_DNG_EPP         0x03
#define PCAN_TYPE_DNG_SJA         0x05
#define PCAN_TYPE_DNG_SJA_EPP     0x06
#define PCAN_TYPE_PCI             0x07
#define PCAN_TYPE_PCI_SJA         0x0D
#define PCAN_TYPE_USB             0x08
#define PCAN_TYPE_PCIEXPRESS      0x0E

#define PCAN_PARAM_DEVICE_ID      0x101
#define PCAN_PARAM_DEVICE_NUMBER  0x102
#define PCAN_PARAM_CONTROLLER_NUMBER 0x103

#define PCAN_ATTACHED_CHANNELS    0x106  // 获取已连接通道
#define PCAN_CHANNEL_CONDITION    0x10C  // 通道条件(可用/占用)
#define PCAN_CHANNEL_IDENTIFYING  0x10D  // 通道识别
#define PCAN_CHANNEL_FEATURES     0x10E  // 通道特性

// TPCANMsg 结构体
typedef struct {
    uint32_t ID;
    uint8_t  MSGTYPE;
    uint8_t  LEN;
    uint8_t  DATA[8];
} TPCANMsg;

// TPCANTimestamp 结构体
typedef struct {
    uint32_t millis;
    uint16_t millis_overflow;
    uint16_t micros;
} TPCANTimestamp;

/// PCAN 适配器 —— 动态加载 PCANBasic.dll 实现对 PEAK CAN 设备的访问
class PcanAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit PcanAdapter(QObject *parent = nullptr);
    ~PcanAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "PCAN"; }

    /// 设置读取超时 (ms)
    void setReadTimeout(int ms);

    /// 通道号转设备名
    static QString channelName(int channel);

private:
    // ─── 动态加载的函数指针 ───
    typedef uint32_t (__stdcall *CAN_Initialize_t)(uint16_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (__stdcall *CAN_Uninitialize_t)(uint16_t);
    typedef uint32_t (__stdcall *CAN_Reset_t)(uint16_t);
    typedef uint32_t (__stdcall *CAN_GetStatus_t)(uint16_t);
    typedef uint32_t (__stdcall *CAN_Read_t)(uint16_t, TPCANMsg*, TPCANTimestamp*);
    typedef uint32_t (__stdcall *CAN_Write_t)(uint16_t, TPCANMsg*);
    typedef uint32_t (__stdcall *CAN_FilterMessages_t)(uint16_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (__stdcall *CAN_GetValue_t)(uint16_t, uint32_t, void*, uint32_t);
    typedef uint32_t (__stdcall *CAN_SetValue_t)(uint16_t, uint32_t, void*, uint32_t);
    typedef uint32_t (__stdcall *CAN_GetErrorText_t)(uint32_t, uint16_t, char*);

    CAN_Initialize_t     m_Initialize = nullptr;
    CAN_Uninitialize_t   m_Uninitialize = nullptr;
    CAN_Reset_t          m_Reset = nullptr;
    CAN_GetStatus_t      m_GetStatus = nullptr;
    CAN_Read_t           m_Read = nullptr;
    CAN_Write_t          m_Write = nullptr;
    CAN_FilterMessages_t m_FilterMessages = nullptr;
    CAN_GetValue_t       m_GetValue = nullptr;
    CAN_SetValue_t       m_SetValue = nullptr;
    CAN_GetErrorText_t   m_GetErrorText = nullptr;

    bool loadLibrary();
    void unloadLibrary();
    static int channelDeviceType(int channel);
    QString errorText(uint32_t err);

    QLibrary *m_library = nullptr;
    uint16_t  m_channel = 0;
    bool      m_loaded = false;
    bool      m_opened = false;
    int       m_readTimeoutMs = 1;

    static constexpr int kMaxChannel = 16;
};

#endif // PCANADAPTER_H
