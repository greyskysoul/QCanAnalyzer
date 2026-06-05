#ifndef ZCANADAPTER_H
#define ZCANADAPTER_H

#include "can/caninterface.h"
#include "third_party/zcanfd/controlcanfd.h"
#include <QLibrary>
#include <QTimer>

// controlcanfd.h 未提供的旧版设备类型常量
#ifndef VCI_USBCAN1
#define VCI_USBCAN1     3
#define VCI_USBCAN2     4
#endif

/// ZCAN 适配器 (ZLG USBCAN 系列旧版设备, VCI API)
/// Windows: 动态加载 ControlCAN.dll
class ZcanAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit ZcanAdapter(QObject *parent = nullptr);
    ~ZcanAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "ZCAN"; }

    static QString channelName(int channel);

private:
    void onReadTimer();
    void pollMessages();
    UINT timing0ForBaud(CanBaudRate baud) const;
    UINT timing1ForBaud(CanBaudRate baud) const;

    // ─── 函数指针 (Windows 动态加载) ───
    typedef DWORD (__stdcall *VCI_OpenDevice_t)(DWORD, DWORD, DWORD);
    typedef DWORD (__stdcall *VCI_CloseDevice_t)(DWORD, DWORD);
    typedef DWORD (__stdcall *VCI_InitCAN_t)(DWORD, DWORD, DWORD, VCI_INIT_CONFIG*);
    typedef DWORD (__stdcall *VCI_ReadBoardInfo_t)(DWORD, DWORD, VCI_BOARD_INFO*);
    typedef DWORD (__stdcall *VCI_StartCAN_t)(DWORD, DWORD, DWORD);
    typedef DWORD (__stdcall *VCI_ResetCAN_t)(DWORD, DWORD, DWORD);
    typedef ULONG (__stdcall *VCI_GetReceiveNum_t)(DWORD, DWORD, DWORD);
    typedef DWORD (__stdcall *VCI_ClearBuffer_t)(DWORD, DWORD, DWORD);
    typedef ULONG (__stdcall *VCI_Transmit_t)(DWORD, DWORD, DWORD, VCI_CAN_OBJ*, ULONG);
    typedef ULONG (__stdcall *VCI_Receive_t)(DWORD, DWORD, DWORD, VCI_CAN_OBJ*, ULONG, INT);

    VCI_OpenDevice_t     m_openDevice = nullptr;
    VCI_CloseDevice_t    m_closeDevice = nullptr;
    VCI_InitCAN_t        m_initCAN = nullptr;
    VCI_ReadBoardInfo_t  m_readBoardInfo = nullptr;
    VCI_StartCAN_t       m_startCAN = nullptr;
    VCI_ResetCAN_t       m_resetCAN = nullptr;
    VCI_GetReceiveNum_t  m_getReceiveNum = nullptr;
    VCI_ClearBuffer_t    m_clearBuffer = nullptr;
    VCI_Transmit_t       m_transmit = nullptr;
    VCI_Receive_t        m_receive = nullptr;

    bool loadLibrary();
    void unloadLibrary();
    QLibrary *m_library = nullptr;
    bool      m_loaded = false;

    // ─── 设备状态 ───
    DWORD   m_deviceType = VCI_USBCAN2;  // 默认 USB CAN-II
    DWORD   m_deviceIndex = 0;
    DWORD   m_canIndex = 0;       // 通道号 (0 或 1)
    bool    m_opened = false;

    // ─── 读取轮询 ───
    QTimer *m_readTimer = nullptr;
};

#endif // ZCANADAPTER_H
