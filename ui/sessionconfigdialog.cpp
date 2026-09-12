#include "sessionconfigdialog.h"
#include "ui_sessionconfigdialog.h"
#ifndef Q_OS_LINUX
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#endif
#include "can/zcanfdadapter.h"
#ifndef Q_OS_LINUX
#include "can/zcanadapter.h"
#endif
#include "can/socketcanadapter.h"
#ifdef QT_DEBUG
#include "can/mockcanadapter.h"
#endif

#include <QPushButton>
#include <QMessageBox>
#include <QEvent>

SessionConfigDialog::SessionConfigDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SessionConfigDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setMinimumSize(460, 380);

#ifdef Q_OS_LINUX
    ui->adapterCombo->addItem("SocketCAN", static_cast<int>(CanAdapterType::SocketCAN));
#else
    ui->adapterCombo->addItem("PCAN", static_cast<int>(CanAdapterType::PCAN));
    ui->adapterCombo->addItem("gs_usb (candleLight)", static_cast<int>(CanAdapterType::GsUsb));
#endif
    ui->adapterCombo->addItem("ZCANFD (USBCANFD)", static_cast<int>(CanAdapterType::ZCANFD));
#ifndef Q_OS_LINUX
    ui->adapterCombo->addItem("ZCAN (USBCAN)", static_cast<int>(CanAdapterType::ZCAN));
#endif
#ifdef QT_DEBUG
    ui->adapterCombo->addItem(tr("MockCAN (虚拟调试)"), static_cast<int>(CanAdapterType::MockCan));
#endif

    // 切换适配器只重置列表，不自动扫描（扫描可能阻塞）
    connect(ui->adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SessionConfigDialog::onAdapterChanged);

    ui->refreshBtn->setStyleSheet(
        "QPushButton { background-color: #607d8b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #455a64; }");
    connect(ui->refreshBtn, &QPushButton::clicked, this, &SessionConfigDialog::scanDevices);

    ui->baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    ui->baudCombo->setCurrentText("500K");

    connect(ui->canFdChk, &QCheckBox::toggled, this, &SessionConfigDialog::onCanFdToggled);

    ui->dataBaudCombo->addItems({"2M", "4M", "5M", "8M", "10M"});
    ui->dataBaudCombo->setCurrentText("2M");
    ui->fdGroup->setVisible(false);

    ui->statusLabel->setStyleSheet("color: #7f8c8d; font-size: 12px;");

    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("创建会话"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #2980b9; }"
        "QPushButton:disabled { background-color: #bdc3c7; color: #95a5a6; }");
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setStyleSheet(
        "QPushButton { background-color: #607d8b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #455a64; }");

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (ui->deviceCombo->currentData().toInt() < 0) {
            QMessageBox::warning(this, tr("提示"), tr("请选择有效的 CAN 设备"));
            return;
        }
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onAdapterChanged();
}

SessionConfigDialog::~SessionConfigDialog()
{
    delete ui;
}

void SessionConfigDialog::onAdapterChanged()
{
    ui->deviceCombo->clear();
    ui->deviceCombo->addItem(tr("点击「刷新」扫描设备"), -1);
    ui->statusLabel->setText(tr("请点击刷新按钮扫描设备"));
    if (auto *btn = ui->buttonBox->button(QDialogButtonBox::Ok))
        btn->setEnabled(false);
}

void SessionConfigDialog::scanDevices()
{
    QString current = ui->deviceCombo->currentText();
    ui->deviceCombo->clear();

    int adapterType = ui->adapterCombo->currentData().toInt();
    QList<CanDeviceInfo> devices;

    switch (static_cast<CanAdapterType>(adapterType)) {
#ifndef Q_OS_LINUX
    case CanAdapterType::PCAN: {
        PcanAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
    case CanAdapterType::GsUsb: {
        GsUsbAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
#endif
    case CanAdapterType::ZCANFD: {
        ZcanFdAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
#ifndef Q_OS_LINUX
    case CanAdapterType::ZCAN: {
        ZcanAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
#endif
#ifdef Q_OS_LINUX
    case CanAdapterType::SocketCAN: {
        SocketCanAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
#endif
#ifdef QT_DEBUG
    case CanAdapterType::MockCan: {
        MockCanAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
#endif
    }

    if (devices.isEmpty()) {
        ui->deviceCombo->addItem(tr("未检测到设备"), -1);

#ifdef Q_OS_LINUX
        if (adapterType == static_cast<int>(CanAdapterType::SocketCAN))
            ui->statusLabel->setText(tr("⚠ 请使用 ip link 命令配置 CAN 接口波特率\n"
                                   "   例: sudo ip link set can0 type can bitrate 500000"));
        else
#endif
            ui->statusLabel->setText(tr("⚠ 未检测到设备，请检查连接和驱动"));
        if (adapterType == static_cast<int>(CanAdapterType::ZCAN)
            || adapterType == static_cast<int>(CanAdapterType::ZCANFD)) {
            ui->statusLabel->setToolTip(tr("如已连接ZCAN设备, 请断开所有ZCAN会话后重新扫描"));
        }
    } else {
        for (const auto &dev : devices) {
            // PCAN 每个通道单独成条，description 里含通道状态；其余适配器 name 已足够
            ui->deviceCombo->addItem(adapterType == static_cast<int>(CanAdapterType::PCAN)
                                     ? dev.description : dev.name, dev.channel);
        }
        ui->statusLabel->setText(tr("✓ 检测到 %1 个设备").arg(devices.size()));
        if (adapterType == static_cast<int>(CanAdapterType::ZCAN)
            || adapterType == static_cast<int>(CanAdapterType::ZCANFD)) {
            ui->statusLabel->setToolTip(tr("已连接的ZCAN设备不会被重新扫描\n断开所有ZCAN会话后可获取最新设备列表"));
        }
    }

    int idx = ui->deviceCombo->findText(current, Qt::MatchStartsWith);
    if (idx >= 0) ui->deviceCombo->setCurrentIndex(idx);

    if (auto *btn = ui->buttonBox->button(QDialogButtonBox::Ok))
        btn->setEnabled(ui->deviceCombo->currentData().toInt() >= 0);
}

void SessionConfigDialog::onCanFdToggled(bool checked)
{
    if (checked) {
        ui->baudLabel->setText(tr("仲裁域波特率:"));
        ui->fdGroup->setVisible(true);
    } else {
        ui->baudLabel->setText(tr("波特率:"));
        ui->fdGroup->setVisible(false);
    }
    adjustSize();
}

void SessionConfigDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("创建会话"));
        ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
        // retranslateUi 会把标签复位为 .ui 文本，需按当前 FD 状态重建
        ui->baudLabel->setText(ui->canFdChk->isChecked() ? tr("仲裁域波特率:")
                                                        : tr("波特率:"));
        onAdapterChanged();
    }
    QDialog::changeEvent(event);
}

bool SessionConfigDialog::configure(int &channel, CanBaudRate &baud, bool &isCanFd,
                                    QString &dataBaudText, int &adapterType, QString &deviceName)
{
    if (exec() != QDialog::Accepted)
        return false;

    adapterType = ui->adapterCombo->currentData().toInt();
    channel = ui->deviceCombo->currentData().toInt();
    deviceName = ui->deviceCombo->currentText().section("  [", 0, 0).trimmed();
    baud = baudRateFromString(ui->baudCombo->currentText());
    isCanFd = ui->canFdChk->isChecked();
    dataBaudText = isCanFd ? ui->dataBaudCombo->currentText() : QString();

    return true;
}
