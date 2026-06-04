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
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTimer>
#include <QLayout>

QT_BEGIN_NAMESPACE
namespace Ui { class CanSessionWidget; }
QT_END_NAMESPACE

class PcanAdapter;
class GsUsbAdapter;
class SocketCanAdapter;

/// 单个 CAN 会话面板 —— 作为可停靠的独立窗口
class CanSessionWidget : public QWidget
{
    Q_OBJECT

public:
    /// 接收表格列索引
    enum RxTableColumn {
        ColTime = 0,
        ColId = 1,
        ColCh = 2,     // 通道
        ColType = 3,
        ColDlc = 4,
        ColData = 5,
        ColDir = 6
    };

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
    void onSendOneFrame();
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
    void stopSending();
    void updateSendButtonState(bool sending);

    Ui::CanSessionWidget *ui;

    int m_sessionId;

    // ─── 通道接收复选框 ───
    QList<QCheckBox*> m_channelChks;

    // ─── 定时器 ───
    QTimer      *m_periodicTimer = nullptr;
    QTimer      *m_statusTimer = nullptr;
    QTimer      *m_frameTimer = nullptr;   // 逐帧发送定时器

    // ─── 发送状态 ───
    int          m_frameRemaining = 0;     // 剩余待发送帧数
    bool         m_sending = false;        // 是否正在发送
    CanMessage   m_pendingMsg;             // 待发送的消息模板

    // ─── CAN 接口 ───
    CanInterface *m_can = nullptr;
    PcanAdapter  *m_pcan = nullptr;
    GsUsbAdapter *m_gsusb = nullptr;
    SocketCanAdapter *m_socketcan = nullptr;
    int          m_currentChannel = 0;
    int          m_adapterType = 0;

    // ─── 统计 ───
    int m_rxCount = 0;
    int m_txCount = 0;
    int m_maxTableRows = 5000; // 最大表格行数
};

#endif // CANSESSIONWIDGET_H
