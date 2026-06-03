#ifndef SESSIONCONFIGDIALOG_H
#define SESSIONCONFIGDIALOG_H

#include "can/caninterface.h"
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QLabel>

/// 新建会话时的参数配置对话框
class SessionConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SessionConfigDialog(QWidget *parent = nullptr);

    /// 运行对话框，返回 true 表示用户点击了确定
    bool configure(int &channel, CanBaudRate &baud, bool &isCanFd);

private:
    void scanDevices();

    QComboBox      *m_deviceCombo;
    QComboBox      *m_baudCombo;
    QCheckBox      *m_canFdChk;
    QLabel         *m_statusLabel;
    QDialogButtonBox *m_buttonBox;
};

#endif // SESSIONCONFIGDIALOG_H
