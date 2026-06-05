#ifndef __CONTROLCANFD_H__
#define __CONTROLCANFD_H__

#include <iostream>
#include "config.h"
using namespace std;

#define CONTROLCANFD_API __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C"
{
#endif

#pragma pack(push, 1)

#define UINT unsigned int
#define BYTE unsigned char
#define USHORT unsigned short
#define UCHAR unsigned char
#define UINT64 unsigned long long
#define CHAR char
#define DWORD unsigned int
#define ULONG unsigned long
#define PVOID void *
#define INT int

/* special address description flags for the MAKE_CAN_ID */
#define CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U /* error message frame */
#define CAN_ID_FLAG 0x1FFFFFFFU  /* id */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */
// make id
#define MAKE_CAN_ID(id, eff, rtr, err) (id | (!!(eff) << 31) | (!!(rtr) << 30) | (!!(err) << 29))
#define IS_EFF(id) (!!(id & CAN_EFF_FLAG)) // 1:extend frame 0:standard frame
#define IS_RTR(id) (!!(id & CAN_RTR_FLAG)) // 1:remote frame 0:data frame
#define IS_ERR(id) (!!(id & CAN_ERR_FLAG)) // 1:error frame 0:normal frame
#define GET_ID(id) (id & CAN_ID_FLAG)

#define CAN_MAX_DLEN 8
#define CANFD_MAX_DLEN 64
    typedef struct
    {
        UINT can_id;  /* 32 bit MAKE_CAN_ID + EFF/RTR/ERR flags */
        BYTE can_dlc; /* frame payload length in byte (0 .. CAN_MAX_DLEN) */
        BYTE __pad;   /* padding */
        BYTE __res0;  /* reserved / padding */
        BYTE __res1;  /* reserved / padding */
        BYTE data[CAN_MAX_DLEN] /* __attribute__((aligned(8)))*/;
    } can_frame;

    typedef struct
    {
        UINT can_id; /* 32 bit MAKE_CAN_ID + EFF/RTR/ERR flags */
        BYTE len;    /* frame payload length in byte */
        BYTE flags;  /* additional flags for CAN FD,i.e error code */
        BYTE __res0; /* reserved / padding */
        BYTE __res1; /* reserved / padding */
        BYTE data[CANFD_MAX_DLEN] /* __attribute__((aligned(8)))*/;
    } canfd_frame;

// 接口卡类型定义
#define USBCANFD_200U 41
#define VCI_USBCAN_E_U 20
#define VCI_USBCAN_2E_U 21
    /*
    #define ZCAN_ERROR_CAN_OVERFLOW            0x0001
    #define ZCAN_ERROR_CAN_ERRALARM            0x0002
    #define	ZCAN_ERROR_CAN_PASSIVE             0x0004
    #define	ZCAN_ERROR_CAN_LOSE                0x0008
    #define	ZCAN_ERROR_CAN_BUSERR              0x0010
    #define ZCAN_ERROR_CAN_BUSOFF              0x0020
    #define ZCAN_ERROR_CAN_BUFFER_OVERFLOW     0x0040

    #define	ZCAN_ERROR_DEVICEOPENED            0x0100
    #define	ZCAN_ERROR_DEVICEOPEN              0x0200
    #define	ZCAN_ERROR_DEVICENOTOPEN           0x0400
    #define	ZCAN_ERROR_BUFFEROVERFLOW          0x0800
    #define	ZCAN_ERROR_DEVICENOTEXIST          0x1000
    #define	ZCAN_ERROR_LOADKERNELDLL           0x2000
    #define ZCAN_ERROR_CMDFAILED               0x4000
    #define	ZCAN_ERROR_BUFFERCREATE            0x8000

    #define ZCAN_ERROR_CANETE_PORTOPENED       0x00010000
    #define ZCAN_ERROR_CANETE_INDEXUSED        0x00020000
    #define ZCAN_ERROR_REF_TYPE_ID             0x00030001
    #define ZCAN_ERROR_CREATE_SOCKET           0x00030002
    #define ZCAN_ERROR_OPEN_CONNECT            0x00030003
    #define ZCAN_ERROR_NO_STARTUP              0x00030004
    #define ZCAN_ERROR_NO_CONNECTED            0x00030005
    #define ZCAN_ERROR_SEND_PARTIAL            0x00030006
    #define ZCAN_ERROR_SEND_TOO_FAST           0x00030007
    */

#define STATUS_ERR 0
#define STATUS_OK 1
#define STATUS_ONLINE 2
#define STATUS_OFFLINE 3
#define STATUS_UNSUPPORTED 4

#define CMD_DESIP 0
#define CMD_DESPORT 1
#define CMD_CHGDESIPANDPORT 2
#define CMD_SRCPORT 2
#define CMD_TCP_TYPE 4
#define TCP_CLIENT 0
#define TCP_SERVER 1

#define CMD_CLIENT_COUNT 5
#define CMD_CLIENT 6
#define CMD_DISCONN_CLINET 7
#define CMD_SET_RECONNECT_TIME 8

#define TYPE_CAN 0
#define TYPE_CANFD 1

    typedef void *DEVICE_HANDLE;
    typedef void *CHANNEL_HANDLE;

    typedef struct tagZCAN_DEVICE_INFO
    {
        USHORT hw_Version;
        USHORT fw_Version;
        USHORT dr_Version;
        USHORT in_Version;
        USHORT irq_Num;
        BYTE can_Num;
        // 20个字符长度USBCANFD202408094469，原定义为20，长度不够
        UCHAR str_Serial_Num[21];
        UCHAR str_hw_Type[40];
        USHORT reserved[4];
    } ZCAN_DEVICE_INFO;

    typedef struct tagZCAN_CHANNEL_INIT_CONFIG
    {
        UINT can_type; // type:TYPE_CAN TYPE_CANFD
        union
        {
            struct
            {
                UINT acc_code;
                UINT acc_mask;
                UINT reserved;
                BYTE filter;
                BYTE timing0;
                BYTE timing1;
                BYTE mode;
            } can;
            struct
            {
                UINT acc_code;
                UINT acc_mask;
                UINT abit_timing;
                UINT dbit_timing;
                UINT brp;
                BYTE filter;
                BYTE mode;
                USHORT pad;
                UINT reserved;
            } canfd;
        };
    } ZCAN_CHANNEL_INIT_CONFIG;

    typedef struct tagZCAN_CHANNEL_ERR_INFO
    {
        UINT error_code;
        BYTE passive_ErrData[3];
        BYTE arLost_ErrData;
    } ZCAN_CHANNEL_ERR_INFO;

    typedef struct tagZCAN_CHANNEL_STATUS
    {
        BYTE errInterrupt;
        BYTE regMode;
        BYTE regStatus;
        BYTE regALCapture;
        BYTE regECCapture;
        BYTE regEWLimit;
        BYTE regRECounter;
        BYTE regTECounter;
        UINT Reserved;
    } ZCAN_CHANNEL_STATUS;

    typedef struct tagZCAN_Transmit_Data
    {
        can_frame frame;
        UINT transmit_type;
    } ZCAN_Transmit_Data;

    typedef struct tagZCAN_Receive_Data
    {
        can_frame frame;
        UINT64 timestamp; // us
    } ZCAN_Receive_Data;

    typedef struct tagZCAN_TransmitFD_Data
    {
        canfd_frame frame;
        UINT transmit_type;
    } ZCAN_TransmitFD_Data;

    typedef struct tagZCAN_ReceiveFD_Data
    {
        canfd_frame frame;
        UINT64 timestamp; // us
    } ZCAN_ReceiveFD_Data;

    typedef struct tagZCAN_AUTO_TRANSMIT_OBJ
    {
        USHORT enable;
        USHORT index;  // 0...n
        UINT interval; // ms
        ZCAN_Transmit_Data obj;
    } ZCAN_AUTO_TRANSMIT_OBJ, *PZCAN_AUTO_TRANSMIT_OBJ;

    typedef struct tagZCANFD_AUTO_TRANSMIT_OBJ
    {
        USHORT enable;
        USHORT index;  // 0...n
        UINT interval; // ms
        ZCAN_TransmitFD_Data obj;
    } ZCANFD_AUTO_TRANSMIT_OBJ, *PZCANFD_AUTO_TRANSMIT_OBJ;

    // 用于设置定时发送额外的参数, 目前只支持USBCANFD-X00U系列设备
    typedef struct tagZCAN_AUTO_TRANSMIT_OBJ_PARAM
    {
        USHORT index; // 定时发送帧的索引
        USHORT type;  // 参数类型，目前类型只有1：表示启动延时
        UINT value;   // 参数数值
    } ZCAN_AUTO_TRANSMIT_OBJ_PARAM, *PZCAN_AUTO_TRANSMIT_OBJ_PARAM;

// for zlg cloud
#define ZCLOUD_MAX_DEVICES 100
#define ZCLOUD_MAX_CHANNEL 16

    typedef struct tagZCLOUD_CHNINFO
    {
        BYTE enable; // 0:disable, 1:enable
        BYTE type;   // 0:CAN, 1:ISO CANFD, 2:Non-ISO CANFD
        BYTE isUpload;
        BYTE isDownload;
    } ZCLOUD_CHNINFO;

    typedef struct tagZCLOUD_DEVINFO
    {
        int devIndex;
        char type[64];
        char id[64];
        char owner[64];
        char model[64];
        char fwVer[16];
        char hwVer[16];
        char serial[64];
        int status; // 0:online, 1:offline
        BYTE bGpsUpload;
        BYTE channelCnt;
        ZCLOUD_CHNINFO channels[ZCLOUD_MAX_CHANNEL];
    } ZCLOUD_DEVINFO;

    typedef struct tagZCLOUD_USER_DATA
    {
        char username[64];
        char mobile[64];
        size_t devCnt;
        ZCLOUD_DEVINFO devices[ZCLOUD_MAX_DEVICES];
    } ZCLOUD_USER_DATA;

    // GPS
    typedef struct tagZCLOUD_GPS_FRAME
    {
        float latitude;
        float longitude;
        float speed;
        struct __gps_time
        {
            USHORT year;
            USHORT mon;
            USHORT day;
            USHORT hour;
            USHORT min;
            USHORT sec;
        } tm;
    } ZCLOUD_GPS_FRAME;
    // for zlg cloud

    // TX timestamp
    typedef struct tagUSBCANFDTxTimeStamp
    {
        UINT *pTxTimeStampBuffer;   // allocated by user, size:nBufferTimeStampCount * 4,unit:100us
        UINT nBufferTimeStampCount; // buffer size
    } USBCANFDTxTimeStamp;

    typedef struct tagTxTimeStamp
    {
        UINT64 *pTxTimeStampBuffer; // allocated by user, size:nBufferTimeStampCount * 8,unit:1us
        UINT nBufferTimeStampCount; // buffer timestamp count
        int nWaitTime;              // Wait Time ms, -1表示等到有数据才返回
    } TxTimeStamp;

    // Bus usage
    typedef struct tagBusUsage
    {
        UINT64 nTimeStampBegin; // 测量起始时间戳，单位us
        UINT64 nTimeStampEnd;   // 测量结束时间戳，单位us
        BYTE nChnl;             // 通道
        BYTE nReserved;         // 保留
        USHORT nBusUsage;       // 总线利用率(%),总线利用率*100展示。取值0~10000，如8050表示80.50%
        UINT nFrameCount;       // 帧数量
    } BusUsage;

    // LIN
    typedef struct _VCI_LIN_MSG
    {
        BYTE ID;
        BYTE DataLen;
        USHORT Flag;
        UINT TimeStamp;
        BYTE Data[8];
    } ZCAN_LIN_MSG, *PZCAN_LIN_MSG;

#define LIN_MODE_MASTER 0
#define LIN_MODE_SLAVE 1
#define LIN_FLAG_CHK_ENHANCE 0x01
#define LIN_FLAG_VAR_DLC 0x02

    typedef struct _VCI_LIN_INIT_CONFIG
    {
        BYTE linMode;
        BYTE linFlag;
        USHORT reserved;
        UINT linBaud;
    } ZCAN_LIN_INIT_CONFIG, *PZCAN_LIN_INIT_CONFIG;
    // end LIN

    //********************以下内容与ControlCAN.h相同************************************************************//

    /*------------兼容ZLG的函数及数据类型---------------------------------*/

    // 1.ZLG系列CAN接口卡信息的数据类型。
    typedef struct _VCI_BOARD_INFO
    {
        USHORT hw_Version;
        USHORT fw_Version;
        USHORT dr_Version;
        USHORT in_Version;
        USHORT irq_Num;
        BYTE can_Num;
        CHAR str_Serial_Num[20];
        CHAR str_hw_Type[40];
        USHORT Reserved[4];
    } VCI_BOARD_INFO, *PVCI_BOARD_INFO; // 共63个字节

    // 2.定义CAN信息帧的数据类型。           现在用到。
    typedef struct _VCI_CAN_OBJ
    { // 32字节。

        UINT ID;
        UINT TimeStamp;  // 保留不用       32位，四字节。
        BYTE TimeFlag;   // 保留不用
        BYTE SendType;   // 保留不用
        BYTE RemoteFlag; // 是否是远程帧
        BYTE ExternFlag; // 是否是扩展帧
        BYTE DataLen;
        BYTE Data[8];
        BYTE Reserved[3]; // Reserved[0]~kCanIndex(for CAN receive)
    } VCI_CAN_OBJ, *PVCI_CAN_OBJ;

    /*
    //2.定义CAN信息帧的数据类型。
    typedef  struct  _VCI_CAN_OBJ{         //32字节。
        VCI_CAN_OBJ1 frame;
        BYTE	Reserved[3];//Reserved[0]~kCanIndex(for CAN receive)
    }VCI_CAN_OBJ,*PVCI_CAN_OBJ;
    */
    // 3.定义CAN控制器状态的数据类型。
    typedef struct _VCI_CAN_STATUS
    {
        UCHAR ErrInterrupt;
        UCHAR regMode;
        UCHAR regStatus;
        UCHAR regALCapture;
        UCHAR regECCapture;
        UCHAR regEWLimit;
        UCHAR regRECounter;
        UCHAR regTECounter;
        DWORD Reserved;
    } VCI_CAN_STATUS, *PVCI_CAN_STATUS;

    // 4.定义错误信息的数据类型。
    typedef struct _ERR_INFO
    {
        UINT ErrCode;
        BYTE Passive_ErrData[3];
        BYTE ArLost_ErrData;
    } VCI_ERR_INFO, *PVCI_ERR_INFO;

    // 5.定义初始化CAN的数据类型
    typedef struct _INIT_CONFIG
    {
        DWORD AccCode;
        DWORD AccMask;
        DWORD Reserved;
        UCHAR Filter; // 0,1接收所有帧。2标准帧滤波，3是扩展帧滤波。
        //	UCHAR	kCanBaud;
        UCHAR Timing0;
        UCHAR Timing1;
        UCHAR Mode; // 模式，0表示正常模式，1表示只听模式,2自测模式
        //	UCHAR	CanRx_IER;//added by yyd
    } VCI_INIT_CONFIG, *PVCI_INIT_CONFIG;

    ///////// new add struct for filter /////////
    typedef struct _VCI_FILTER_RECORD
    {
        DWORD ExtFrame; // 是否为扩展帧
        DWORD Start;
        DWORD End;
    } VCI_FILTER_RECORD, *PVCI_FILTER_RECORD;

#define INVALID_DEVICE_HANDLE 0

#define INVALID_HANDLE_VALUE -1

#define INVALID_CHANNEL_HANDLE 0

//******************************************************************************************************
//******************************************************************************************************

//******************************************************************************************************

/********************************************************
    通信协议V1.0
**/
#define PKG_HEAD 0xBEEF
#define PKG_TAIL 0xDEAD
#define PKG_DATA_MAX_LEN 500
#define PKG_MAGIC 0xBEAD

#define REG8(Addr, offset) *(BYTE *)((BYTE *)(Addr) + (offset))
// 小端模式 16位寄存器
#define REG16_L(Addr, offset) *(USHORT *)((USHORT *)(Addr) + (offset))
// 大端模式16位寄存器--协议为大端模式
#define REG16(Addr, offset) ((REG16_L(Addr, offset) & 0xff) << 8) + ((REG16_L(Addr, offset) >> 8) & 0xff)
#define REG32(Addr, offset) *(UINT *)((UINT *)(Addr) + (offset))

    typedef struct __package__
    {
        USHORT head;
        USHORT datalen;
        BYTE pkgall;
        BYTE pkgcur;
        USHORT cmd;
        BYTE data[PKG_DATA_MAX_LEN];
        USHORT check;
        USHORT tail;
    } PACKAGE;

#define USBCAN_MAX_NUM 128 // 定义USBCAN最大支持数量

#define USBCANFD_MAX_NUM 8      // 64//定义USBCAN最大支持数量
#define USBCANFD_MAX_CHANNEL 16 // 128//定义USBCAN最大支持数量

    //**********

    DEVICE_HANDLE CONTROLCANFD_API ZCAN_OpenDevice(UINT deviceType, UINT deviceIndex, UINT reserved);

    DEVICE_HANDLE CONTROLCANFD_API ZCAN_OpenDeviceEx(UINT deviceType, UINT deviceIndex, DWORD targetBus, DWORD targetAddress, UINT reserved);

    UINT CONTROLCANFD_API ZCAN_CloseDevice(DEVICE_HANDLE device_handle);
    UINT CONTROLCANFD_API ZCAN_GetDeviceInf(DEVICE_HANDLE device_handle, ZCAN_DEVICE_INFO *pInfo);
    UINT CONTROLCANFD_API ZCAN_IsDeviceOnLine(DEVICE_HANDLE device_handle);
    CHANNEL_HANDLE CONTROLCANFD_API ZCAN_InitCAN(DEVICE_HANDLE device_handle, UINT can_index, ZCAN_CHANNEL_INIT_CONFIG *pInitConfig);
    UINT CONTROLCANFD_API ZCAN_StartCAN(CHANNEL_HANDLE channel_handle);
    UINT CONTROLCANFD_API ZCAN_ResetCAN(CHANNEL_HANDLE channel_handle);
    UINT CONTROLCANFD_API ZCAN_ClearBuffer(CHANNEL_HANDLE channel_handle);
    UINT CONTROLCANFD_API ZCAN_GetReceiveNum(CHANNEL_HANDLE channel_handle, BYTE type); // type:TYPE_CAN TYPE_CANFD
    UINT CONTROLCANFD_API ZCAN_Transmit(CHANNEL_HANDLE channel_handle, ZCAN_Transmit_Data *pTransmit, UINT len);
    UINT CONTROLCANFD_API ZCAN_Receive(CHANNEL_HANDLE channel_handle, ZCAN_Receive_Data *pReceive, UINT len, int wait_time);
    UINT CONTROLCANFD_API ZCAN_TransmitFD(CHANNEL_HANDLE channel_handle, ZCAN_TransmitFD_Data *pTransmit, UINT len);
    UINT CONTROLCANFD_API ZCAN_ReceiveFD(CHANNEL_HANDLE channel_handle, ZCAN_ReceiveFD_Data *pReceive, UINT len, int wait_time);
    IProperty *CONTROLCANFD_API GetIProperty(DEVICE_HANDLE device_handle);
    UINT CONTROLCANFD_API ReleaseIProperty(IProperty *pIProperty);
    UINT CONTROLCANFD_API ZCAN_SetAbitBaud(DEVICE_HANDLE device_handle, UINT can_index, UINT abitbaud);
    UINT CONTROLCANFD_API ZCAN_SetDbitBaud(DEVICE_HANDLE device_handle, UINT can_index, UINT dbitbaud);
    UINT CONTROLCANFD_API ZCAN_SetCANFDStandard(DEVICE_HANDLE device_handle, UINT can_index, UINT canfd_standard);
    UINT CONTROLCANFD_API ZCAN_SetResistanceEnable(DEVICE_HANDLE device_handle, UINT can_index, UINT enable);
    UINT CONTROLCANFD_API ZCAN_SetBaudRateCustom(DEVICE_HANDLE device_handle, UINT can_index, char *RateCustom);
    UINT CONTROLCANFD_API ZCAN_ClearFilter(CHANNEL_HANDLE channel_handle);
    UINT CONTROLCANFD_API ZCAN_AckFilter(CHANNEL_HANDLE channel_handle);
    UINT CONTROLCANFD_API ZCAN_SetFilterMode(CHANNEL_HANDLE channel_handle, UINT mode);
    UINT CONTROLCANFD_API ZCAN_SetFilterStartID(CHANNEL_HANDLE channel_handle, UINT start_id);
    UINT CONTROLCANFD_API ZCAN_SetFilterEndID(CHANNEL_HANDLE channel_handle, UINT EndID);

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
