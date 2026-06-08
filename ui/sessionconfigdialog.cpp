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

SessionConfigDialog::SessionConfigDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SessionConfigDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setMinimumSize(460, 380);

    // ── 适配器类型 ──
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
    ui->adapterCombo->addItem("MockCAN (虚拟调试)", static_cast<int>(CanAdapterType::MockCan));
#endif

    // 切换适配器时只更新提示，不自动扫描（避免卡顿）
    connect(ui->adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SessionConfigDialog::onAdapterChanged);

    // 刷新按钮
    ui->refreshBtn->setStyleSheet(
        "QPushButton { background-color: #607d8b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #455a64; }");
    connect(ui->refreshBtn, &QPushButton::clicked, this, &SessionConfigDialog::scanDevices);

    // 波特率列表
    ui->baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    ui->baudCombo->setCurrentText("500K");

    // CAN-FD 复选框
    connect(ui->canFdChk, &QCheckBox::toggled, this, &SessionConfigDialog::onCanFdToggled);

    // CAN-FD 数据域波特率
    ui->dataBaudCombo->addItems({"2M", "4M", "5M", "8M", "10M"});
    ui->dataBaudCombo->setCurrentText("2M");
    ui->fdGroup->setVisible(false);

    // ── 状态标签 ──
    ui->statusLabel->setStyleSheet("color: #7f8c8d; font-size: 12px;");

    // ── 按钮 ──
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText("创建会话");
    ui->buttonBox->button(QDialogButtonBox::Ok)->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #2980b9; }"
        "QPushButton:disabled { background-color: #bdc3c7; color: #95a5a6; }");
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setStyleSheet(
        "QPushButton { background-color: #607d8b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #455a64; }");

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (ui->deviceCombo->currentData().toInt() < 0) {
            QMessageBox::warning(this, "提示", "请选择有效的 CAN 设备");
            return;
        }
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ── 初始状态显示提示 ──
    onAdapterChanged();
}

SessionConfigDialog::~SessionConfigDialog()
{
    delete ui;
}

void SessionConfigDialog::onAdapterChanged()
{
    ui->deviceCombo->clear();
    ui->deviceCombo->addItem("点击「刷新」扫描设备", -1);
    ui->statusLabel->setText("请点击刷新按钮扫描设备");
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
        ui->deviceCombo->addItem("未检测到设备", -1);

#ifdef Q_OS_LINUX
        if (adapterType == static_cast<int>(CanAdapterType::SocketCAN))
            ui->statusLabel->setText("⚠ 请使用 ip link 命令配置 CAN 接口波特率\n"
                                   "   例: sudo ip link set can0 type can bitrate 500000");
        else
#endif
            ui->statusLabel->setText("⚠ 未检测到设备，请检查连接和驱动");
        if (adapterType == static_cast<int>(CanAdapterType::ZCAN)
            || adapterType == static_cast<int>(CanAdapterType::ZCANFD)) {
            ui->statusLabel->setToolTip(QStringLiteral(
                "如已连接ZCAN设备, 请断开所有ZCAN会话后重新扫描"));
        }
    } else {
        for (const auto &dev : devices) {
            // PCAN 每个通道作为独立设备条目，需要显示通道/状态信息
            // 其他适配器的设备名本身已包含标识信息
            if (adapterType == static_cast<int>(CanAdapterType::PCAN)) {
                ui->deviceCombo->addItem(dev.description, dev.channel);
            } else {
                ui->deviceCombo->addItem(dev.name, dev.channel);
            }
        }
        ui->statusLabel->setText(QString("✓ 检测到 %1 个设备").arg(devices.size()));
        if (adapterType == static_cast<int>(CanAdapterType::ZCAN)
            || adapterType == static_cast<int>(CanAdapterType::ZCANFD)) {
            ui->statusLabel->setToolTip(QStringLiteral(
                "已连接的ZCAN设备不会被重新扫描\n断开所有ZCAN会话后可获取最新设备列表"));
        }
    }

    int idx = ui->deviceCombo->findText(current, Qt::MatchStartsWith);
    if (idx >= 0) ui->deviceCombo->setCurrentIndex(idx);

    // 有设备时自动启用 OK 按钮
    if (auto *btn = ui->buttonBox->button(QDialogButtonBox::Ok))
        btn->setEnabled(ui->deviceCombo->currentData().toInt() >= 0);
}

void SessionConfigDialog::onCanFdToggled(bool checked)
{
    if (checked) {
        ui->baudLabel->setText("仲裁域波特率:");
        ui->fdGroup->setVisible(true);
    } else {
        ui->baudLabel->setText("波特率:");
        ui->fdGroup->setVisible(false);
    }
    adjustSize();
}

bool SessionConfigDialog::configure(int &channel, CanBaudRate &baud, bool &isCanFd,
                                    CanBaudRate &dataBaud, int &adapterType, QString &deviceName)
{
    if (exec() != QDialog::Accepted)
        return false;

    adapterType = ui->adapterCombo->currentData().toInt();
    channel = ui->deviceCombo->currentData().toInt();
    deviceName = ui->deviceCombo->currentText().section("  [", 0, 0).trimmed();

    baud = baudRateFromString(ui->baudCombo->currentText());

    isCanFd = ui->canFdChk->isChecked();

    // CAN-FD 数据域波特率
    if (isCanFd) {
        QString dBaudStr = ui->dataBaudCombo->currentText();
        if (dBaudStr == "2M")      dataBaud = CanBaudRate::BR_1M;    // 暂用1M值占位
        else if (dBaudStr == "4M") dataBaud = CanBaudRate::BR_800K;
        else if (dBaudStr == "5M") dataBaud = CanBaudRate::BR_500K;
        else if (dBaudStr == "8M") dataBaud = CanBaudRate::BR_250K;
        else                       dataBaud = CanBaudRate::BR_1M;
    }
    return true;
}
