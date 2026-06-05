#ifndef ZCANFDADAPTER_H
#define ZCANFDADAPTER_H

#include "can/caninterface.h"
#include "third_party/zcanfd/controlcanfd.h"
#include <QLibrary>
#include <QTimer>

/// ZCANFD 适配器 (ZLG USBCANFD 系列设备)
/// Windows: 动态加载 ControlCANFD.dll
/// Linux:   静态链接 libcontrolcanfd.a
class ZcanFdAdapter : public CanInterface
{
    Q_OBJECT

public:
    explicit ZcanFdAdapter(QObject *parent = nullptr);
    ~ZcanFdAdapter() override;

    QList<CanDeviceInfo> scanDevices() override;
    bool open(int channel, CanBaudRate baud = CanBaudRate::BR_500K) override;
    void close() override;
    bool isOpen() const override;
    bool sendMessage(const CanMessage &msg) override;
    bool isAlive() const override;
    QString adapterName() const override { return "ZCANFD"; }

    static QString channelName(int channel);

private:
    void onReadTimer();
    void pollMessages();
    QString errorText(UINT err);
    UINT baudToTiming(CanBaudRate baud) const;

    // ─── 函数指针 (Windows 动态加载) ───
#ifdef Q_OS_WIN
    typedef DEVICE_HANDLE (__stdcall *ZCAN_OpenDevice_t)(UINT, UINT, UINT);
    typedef UINT (__stdcall *ZCAN_CloseDevice_t)(DEVICE_HANDLE);
    typedef UINT (__stdcall *ZCAN_GetDeviceInf_t)(DEVICE_HANDLE, ZCAN_DEVICE_INFO*);
    typedef CHANNEL_HANDLE (__stdcall *ZCAN_InitCAN_t)(DEVICE_HANDLE, UINT, ZCAN_CHANNEL_INIT_CONFIG*);
    typedef UINT (__stdcall *ZCAN_StartCAN_t)(CHANNEL_HANDLE);
    typedef UINT (__stdcall *ZCAN_ResetCAN_t)(CHANNEL_HANDLE);
    typedef UINT (__stdcall *ZCAN_ClearBuffer_t)(CHANNEL_HANDLE);
    typedef UINT (__stdcall *ZCAN_GetReceiveNum_t)(CHANNEL_HANDLE, BYTE);
    typedef UINT (__stdcall *ZCAN_Transmit_t)(CHANNEL_HANDLE, ZCAN_Transmit_Data*, UINT);
    typedef UINT (__stdcall *ZCAN_Receive_t)(CHANNEL_HANDLE, ZCAN_Receive_Data*, UINT, int);
    typedef UINT (__stdcall *ZCAN_TransmitFD_t)(CHANNEL_HANDLE, ZCAN_TransmitFD_Data*, UINT);
    typedef UINT (__stdcall *ZCAN_ReceiveFD_t)(CHANNEL_HANDLE, ZCAN_ReceiveFD_Data*, UINT, int);
    typedef UINT (__stdcall *ZCAN_SetBaudRateCustom_t)(DEVICE_HANDLE, UINT, char*);

    ZCAN_OpenDevice_t       m_openDevice = nullptr;
    ZCAN_CloseDevice_t      m_closeDevice = nullptr;
    ZCAN_GetDeviceInf_t     m_getDeviceInf = nullptr;
    ZCAN_InitCAN_t          m_initCAN = nullptr;
    ZCAN_StartCAN_t         m_startCAN = nullptr;
    ZCAN_ResetCAN_t         m_resetCAN = nullptr;
    ZCAN_ClearBuffer_t      m_clearBuffer = nullptr;
    ZCAN_GetReceiveNum_t    m_getReceiveNum = nullptr;
    ZCAN_Transmit_t         m_transmit = nullptr;
    ZCAN_Receive_t          m_receive = nullptr;
    ZCAN_TransmitFD_t       m_transmitFD = nullptr;
    ZCAN_ReceiveFD_t        m_receiveFD = nullptr;
    ZCAN_SetBaudRateCustom_t m_setBaudRateCustom = nullptr;

    bool loadLibrary();
    void unloadLibrary();
    QLibrary *m_library = nullptr;
    bool      m_loaded = false;
#endif

    // ─── 设备句柄 ───
    DEVICE_HANDLE   m_devHandle = nullptr;
    CHANNEL_HANDLE  m_chHandle = nullptr;
    UINT            m_deviceType = USBCANFD_200U;
    UINT            m_deviceIndex = 0;
    UINT            m_canIndex = 0;      // 通道号 (0 或 1)
    bool            m_opened = false;
    bool            m_isFdSupported = true; // 是否尝试 CAN FD

    // ─── 读取轮询 ───
    QTimer *m_readTimer = nullptr;
};

#endif // ZCANFDADAPTER_H
