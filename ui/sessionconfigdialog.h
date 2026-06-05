#ifndef SESSIONCONFIGDIALOG_H
#define SESSIONCONFIGDIALOG_H

#include "can/caninterface.h"
#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class SessionConfigDialog; }
QT_END_NAMESPACE

class QComboBox;
class QCheckBox;
class QDialogButtonBox;
class QLabel;

/// 新建会话时的参数配置对话框
class SessionConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SessionConfigDialog(QWidget *parent = nullptr);
    ~SessionConfigDialog();

    /// 运行对话框，返回 true 表示用户点击了确定
    bool configure(int &channel, CanBaudRate &baud, bool &isCanFd,
                   CanBaudRate &dataBaud, int &adapterType, QString &deviceName);

private slots:
    void onCanFdToggled(bool checked);
    void onAdapterChanged();

private:
    void scanDevices();

    Ui::SessionConfigDialog *ui;
};

#endif // SESSIONCONFIGDIALOG_H
