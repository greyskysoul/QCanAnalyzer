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

class PcanAdapter;

/// 单个 CAN 会话面板 —— 作为可停靠的独立窗口
class CanSessionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanSessionWidget(int sessionId, QWidget *parent = nullptr);
    ~CanSessionWidget() override;

    int sessionId() const { return m_sessionId; }

    /// 连接设备
    void connectDevice(int channel, CanBaudRate baud);

    /// 断开设备
    void disconnectDevice();

    /// 是否已连接
    bool isConnected() const;

    /// 扫描可用设备
    void refreshDevices();

private slots:
    void onConnectClicked();
    void onSendClicked();
    void onClearClicked();
    void onMessageReceived(const CanMessage &msg);

private:
    void setupUi();
    void addMessageToTable(const CanMessage &msg);
    void updateStats();

    int m_sessionId;

    // ─── 连接区域 ───
    QComboBox   *m_deviceCombo;
    QComboBox   *m_baudCombo;
    QPushButton *m_connectBtn;
    QPushButton *m_refreshBtn;
    QLabel      *m_statusLabel;

    // ─── 接收表格 ───
    QTableWidget *m_rxTable;
    QLabel       *m_rxCountLabel;
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

    // ─── 统计 ───
    int m_rxCount = 0;
    int m_txCount = 0;
    int m_maxTableRows = 5000; // 表格最大行数限制
};

#endif // CANSESSIONWIDGET_H
