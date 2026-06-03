#ifndef CANSESSIONWIDGET_H
#define CANSESSIONWIDGET_H

#include "can/caninterface.h"
#include "can/canmessage.h"
#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTimer>
#include <QLayout>

class PcanAdapter;
class GsUsbAdapter;
class SocketCanAdapter;

/// 单个 CAN 会话面板 —— 作为可停靠的独立窗口
class CanSessionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanSessionWidget(int sessionId, QWidget *parent = nullptr);
    ~CanSessionWidget() override;

    int sessionId() const { return m_sessionId; }

    /// 连接设备
    void connectDevice(int channel, CanBaudRate baud, int adapterType = 0);

    /// 断开设备
    void disconnectDevice();

    /// 是否已连接
    bool isConnected() const;

    /// 扫描可用设备
    void refreshDevices();

signals:
    /// 设备意外断开通知
    void deviceDisconnected(int sessionId);

private slots:
    void onConnectClicked();
    void onSendClicked();
    void onClearClicked();
    void onSaveClicked();
    void onMessageReceived(const CanMessage &msg);

private:
    void setupUi();
    void linkSignals(CanInterface *iface);
    void addMessageToTable(const CanMessage &msg);
    void updateStats();
    void updateChannelCheckboxes();
    void onStatusCheck();
    void updateUiState(bool connected);

    int m_sessionId;

    // ─── 连接区域 ───
    QLabel      *m_deviceLabel;
    QComboBox   *m_baudCombo;
    QPushButton *m_connectBtn;
    QLabel      *m_statusLabel;

    // ─── 通道接收复选框容器 ───
    QHBoxLayout *m_channelChkLayout;
    QList<QCheckBox*> m_channelChks;

    // ─── 接收表格 ───
    QTableWidget *m_rxTable;
    QLabel       *m_rxCountLabel;
    QPushButton  *m_saveBtn;
    QPushButton  *m_clearBtn;
    QCheckBox    *m_autoScrollChk;

    // ─── 发送区域 ───
    QLineEdit   *m_sendIdEdit;
    QComboBox   *m_sendTypeCombo;
    QSpinBox    *m_sendDlcSpin;
    QLineEdit   *m_sendDataEdit;
    QSpinBox    *m_sendPeriodSpin;
    QPushButton *m_sendBtn;
    QCheckBox   *m_periodicSendChk;
    QTimer      *m_periodicTimer;

    // ─── CAN 接口 ───
    CanInterface *m_can = nullptr;
    PcanAdapter  *m_pcan = nullptr;
    GsUsbAdapter *m_gsusb = nullptr;
    SocketCanAdapter *m_socketcan = nullptr;
    int          m_currentChannel = 0;
    int          m_adapterType = 0;

    // ─── 状态监控定时器 ───
    QTimer       *m_statusTimer = nullptr;

    // ─── 统计 ───
    int m_rxCount = 0;
    int m_txCount = 0;
    int m_maxTableRows = 5000;
};

#endif // CANSESSIONWIDGET_H
