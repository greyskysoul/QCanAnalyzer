#include "sessionconfigdialog.h"
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#include "can/socketcanadapter.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>

SessionConfigDialog::SessionConfigDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("新建 CAN 会话");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    qreal dpr = QApplication::primaryScreen()->devicePixelRatio();
    int dlgW = qMax(400, qRound(460 * dpr / 2));
    int dlgH = qMax(300, qRound(380 * dpr / 2));
    setMinimumSize(dlgW, dlgH);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel("<b style='font-size:14px; color:#2c3e50;'>会话参数配置</b>");
    layout->addWidget(titleLabel);

    auto *form = new QFormLayout();
    form->setSpacing(10);
    form->setContentsMargins(10, 5, 10, 5);

    // ── 适配器类型 ──
    m_adapterCombo = new QComboBox();
    m_adapterCombo->addItem("PCAN", static_cast<int>(CanAdapterType::PCAN));
    m_adapterCombo->addItem("gs_usb (candleLight)", static_cast<int>(CanAdapterType::GsUsb));
#ifdef Q_OS_LINUX
    m_adapterCombo->addItem("SocketCAN", static_cast<int>(CanAdapterType::SocketCAN));
#endif
    form->addRow("适配器:", m_adapterCombo);

    // 切换适配器时自动刷新设备列表
    connect(m_adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SessionConfigDialog::scanDevices);

    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(200);

    auto *refreshBtn = new QPushButton("刷新");
    refreshBtn->setFixedWidth(55);
    refreshBtn->setStyleSheet(
        "QPushButton { background-color: #607d8b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #455a64; }");
    connect(refreshBtn, &QPushButton::clicked, this, &SessionConfigDialog::scanDevices);

    auto *deviceLayout = new QHBoxLayout();
    deviceLayout->addWidget(m_deviceCombo);
    deviceLayout->addWidget(refreshBtn);
    form->addRow("CAN 设备:", deviceLayout);

    m_baudCombo = new QComboBox();
    m_baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    m_baudCombo->setCurrentText("500K");
    form->addRow("仲裁域波特率:", m_baudCombo);

    m_canFdChk = new QCheckBox("启用 CAN-FD (需设备支持)");
    connect(m_canFdChk, &QCheckBox::toggled, this, &SessionConfigDialog::onCanFdToggled);
    form->addRow("", m_canFdChk);

    // ── CAN-FD 数据域波特率 (默认隐藏) ──
    m_fdGroup = new QWidget();
    auto *fdLayout = new QHBoxLayout(m_fdGroup);
    fdLayout->setContentsMargins(0, 0, 0, 0);
    fdLayout->addWidget(new QLabel("数据域波特率:"));
    m_dataBaudCombo = new QComboBox();
    m_dataBaudCombo->addItems({"2M", "4M", "5M", "8M", "10M"});
    m_dataBaudCombo->setCurrentText("2M");
    fdLayout->addWidget(m_dataBaudCombo);
    m_fdGroup->setVisible(false);
    form->addRow("", m_fdGroup);

    layout->addLayout(form);

    // ── 状态 ──
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #7f8c8d; font-size: 12px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    // ── 按钮 ──
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText("创建会话");
    m_buttonBox->button(QDialogButtonBox::Ok)->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #2980b9; }");
    m_buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    m_buttonBox->button(QDialogButtonBox::Cancel)->setStyleSheet(
        "QPushButton { background-color: #607d8b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #455a64; }");

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_deviceCombo->currentData().toInt() <= 0) {
            QMessageBox::warning(this, "提示", "请选择有效的 CAN 设备");
            return;
        }
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(m_buttonBox);

    // ── 最后才扫描设备（确保 m_statusLabel / m_buttonBox 已创建）──
    scanDevices();
}

void SessionConfigDialog::scanDevices()
{
    QString current = m_deviceCombo->currentText();
    m_deviceCombo->clear();

    int adapterType = m_adapterCombo->currentData().toInt();
    QList<CanDeviceInfo> devices;

    switch (static_cast<CanAdapterType>(adapterType)) {
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
    case CanAdapterType::SocketCAN: {
        SocketCanAdapter adapter;
        devices = adapter.scanDevices();
        break;
    }
    }

    if (devices.isEmpty()) {
        m_deviceCombo->addItem("未检测到设备", -1);

#ifdef Q_OS_LINUX
        if (adapterType == static_cast<int>(CanAdapterType::SocketCAN))
            m_statusLabel->setText("⚠ 请使用 ip link 命令配置 CAN 接口波特率\n"
                                   "   例: sudo ip link set can0 type can bitrate 500000");
        else
#endif
            m_statusLabel->setText("⚠ 未检测到设备，请检查连接和驱动");
    } else {
        for (const auto &dev : devices) {
            m_deviceCombo->addItem(QString("%1  [通道 %2]")
                .arg(dev.name)
                .arg(dev.channel), dev.channel);
        }
        m_statusLabel->setText(QString("✓ 检测到 %1 个设备").arg(devices.size()));
    }

    int idx = m_deviceCombo->findText(current, Qt::MatchStartsWith);
    if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);

    // 有设备时自动启用 OK 按钮
    if (auto *btn = m_buttonBox->button(QDialogButtonBox::Ok))
        btn->setEnabled(m_deviceCombo->currentData().toInt() > 0);
}

void SessionConfigDialog::onCanFdToggled(bool checked)
{
    m_fdGroup->setVisible(checked);
    // 调整窗口大小
    adjustSize();
}

bool SessionConfigDialog::configure(int &channel, CanBaudRate &baud, bool &isCanFd,
                                    CanBaudRate &dataBaud, int &adapterType)
{
    if (exec() != QDialog::Accepted)
        return false;

    adapterType = m_adapterCombo->currentData().toInt();
    channel = m_deviceCombo->currentData().toInt();

    QString baudStr = m_baudCombo->currentText();
    if (baudStr == "1M")         baud = CanBaudRate::BR_1M;
    else if (baudStr == "800K")  baud = CanBaudRate::BR_800K;
    else if (baudStr == "250K")  baud = CanBaudRate::BR_250K;
    else if (baudStr == "125K")  baud = CanBaudRate::BR_125K;
    else if (baudStr == "100K")  baud = CanBaudRate::BR_100K;
    else if (baudStr == "50K")   baud = CanBaudRate::BR_50K;
    else if (baudStr == "20K")   baud = CanBaudRate::BR_20K;
    else if (baudStr == "10K")   baud = CanBaudRate::BR_10K;
    else if (baudStr == "5K")    baud = CanBaudRate::BR_5K;
    else                         baud = CanBaudRate::BR_500K;

    isCanFd = m_canFdChk->isChecked();

    // CAN-FD 数据域波特率
    if (isCanFd) {
        QString dBaudStr = m_dataBaudCombo->currentText();
        if (dBaudStr == "2M")      dataBaud = CanBaudRate::BR_1M;    // 暂用1M值占位
        else if (dBaudStr == "4M") dataBaud = CanBaudRate::BR_800K;
        else if (dBaudStr == "5M") dataBaud = CanBaudRate::BR_500K;
        else if (dBaudStr == "8M") dataBaud = CanBaudRate::BR_250K;
        else                       dataBaud = CanBaudRate::BR_1M;
    }
    return true;
}
