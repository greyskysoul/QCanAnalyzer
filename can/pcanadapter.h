#ifndef PCANADAPTER_H
#define PCANADAPTER_H

#include "can/caninterface.h"

// ─── 为 PCANBasic.h 补充所需的 Windows 类型定义（避免引入 <windows.h>）───
#include <cstdint>
typedef uint16_t     WORD;
typedef uint32_t     DWORD;
typedef uint8_t      BYTE;
typedef char*        LPSTR;
typedef uint64_t     UINT64;

#include "third_party/pcan/PCANBasic.h"
#include <QLibrary>
#include <QTimer>

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
    // ─── 动态加载的函数指针 (签名与 PCANBasic.h 一致) ───
    typedef TPCANStatus (__stdcall *CAN_Initialize_t)(TPCANHandle, TPCANBaudrate, BYTE, DWORD, WORD);
    typedef TPCANStatus (__stdcall *CAN_Uninitialize_t)(TPCANHandle);
    typedef TPCANStatus (__stdcall *CAN_Reset_t)(TPCANHandle);
    typedef TPCANStatus (__stdcall *CAN_GetStatus_t)(TPCANHandle);
    typedef TPCANStatus (__stdcall *CAN_Read_t)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
    typedef TPCANStatus (__stdcall *CAN_Write_t)(TPCANHandle, TPCANMsg*);
    typedef TPCANStatus (__stdcall *CAN_FilterMessages_t)(TPCANHandle, DWORD, DWORD, TPCANMode);
    typedef TPCANStatus (__stdcall *CAN_GetValue_t)(TPCANHandle, TPCANParameter, void*, DWORD);
    typedef TPCANStatus (__stdcall *CAN_SetValue_t)(TPCANHandle, TPCANParameter, void*, DWORD);
    typedef TPCANStatus (__stdcall *CAN_GetErrorText_t)(TPCANStatus, WORD, LPSTR);

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
    QString errorText(TPCANStatus err);

    QLibrary *m_library = nullptr;
    TPCANHandle m_channel = 0;
    bool      m_loaded = false;
    bool      m_opened = false;
    int       m_readTimeoutMs = 1;
    QTimer   *m_readTimer = nullptr; // 读取轮询定时器 (复用, 避免泄漏)
};

#endif // PCANADAPTER_H
