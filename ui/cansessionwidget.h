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

class QHexEdit;

QT_BEGIN_NAMESPACE
namespace Ui { class CanSessionWidget; }
QT_END_NAMESPACE

class PcanAdapter;
class GsUsbAdapter;
class ZcanFdAdapter;
#ifndef Q_OS_LINUX
class ZcanAdapter;
#endif
class SocketCanAdapter;
class MockCanAdapter;

/// 单个 CAN 会话面板 —— 作为可停靠的独立窗口
class CanSessionWidget : public QWidget
{
    Q_OBJECT

public:
    /// 接收表格列索引
    enum RxTableColumn {
        ColTime = 0,
        ColDir  = 1,
        ColId   = 2,
        ColCh   = 3,     // 通道
        ColType = 4,
        ColDlc  = 5,
        ColData = 6
    };

    explicit CanSessionWidget(int sessionId, QWidget *parent = nullptr);
    ~CanSessionWidget() override;

    int sessionId() const { return m_sessionId; }

    void connectDevice(int channel, CanBaudRate baud, int adapterType = 0);
    void disconnectDevice();
    bool isConnected() const;

    void setCanFdEnabled(bool enabled);
    bool isCanFdEnabled() const { return m_isCanFd; }

    /// 同步配置页面的波特率到标签页 UI
    void setBaudRateText(const QString &text);
    void setDataBaudRateText(const QString &text);

signals:
    void deviceDisconnected(int sessionId);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onConnectClicked();
    void onSendClicked();
    void onSendOneFrame();
    void onSendDlcChanged(int dlc);
    void onCanFdToggled(bool checked);
    void onClearClicked();
    void onSaveClicked();
    void onFilterChanged();
    void onMessageReceived(const CanMessage &msg);
    void onSendChannelChanged(int index);
    void onStatusCheck();

private:
    void setupUi();
    void linkSignals(CanInterface *iface);
    void retranslateDynamicUi();
    void addMessageToTable(const CanMessage &msg);
    void updateStats();
    void updateChannelCheckboxes();
    void refreshSendChannelCombo();
    void updateUiState(bool connected);
    void updateSendButtonState(bool sending);
    void stopSending();
    void prepareAndStartSend();
    bool passFilter(const CanMessage &msg) const;

    Ui::CanSessionWidget *ui;

    int m_sessionId;

    // 通道接收复选框，channel 值存在 "canChannel" 动态属性中
    QList<QCheckBox*> m_channelChks;

    QTimer *m_statusTimer = nullptr; // 500ms 轮询 isAlive()
    QTimer *m_frameTimer = nullptr;  // 帧间隔发送定时器

    int          m_frameRemaining = 0;   // 剩余待发送帧数
    bool         m_sending = false;
    bool         m_sendUiActive = false; // 发送控件是否处于禁用/“停止”状态
    CanMessage   m_pendingMsg;

    CanInterface *m_can = nullptr;
    PcanAdapter   *m_pcan = nullptr;
    GsUsbAdapter  *m_gsusb = nullptr;
    ZcanFdAdapter  *m_zcanfd = nullptr;
#ifndef Q_OS_LINUX
    ZcanAdapter   *m_zcan = nullptr;
#endif
    SocketCanAdapter *m_socketcan = nullptr;
#ifdef QT_DEBUG
    MockCanAdapter *m_mockcan = nullptr;
#endif
    int            m_currentChannel = 0;
    int            m_channelIndex = 0; // 逻辑通道号（用于显示）
    int            m_adapterType = 0;

    QHexEdit      *m_sendDataEdit = nullptr; // 替换 .ui 中的 sendDataEditHolder

    bool           m_isCanFd = false;

    int m_rxCount = 0;
    int m_txCount = 0;
    static constexpr int kMaxTableRows = 5000;

    struct FilterEntry {
        uint32_t id = 0;
        uint32_t mask = 0; // 1=必须匹配, 0=不关心
    };
    QList<FilterEntry> m_filterEntries;
    bool m_filterEnabled = false;
};

#endif // CANSESSIONWIDGET_H
