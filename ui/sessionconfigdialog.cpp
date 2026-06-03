#include "sessionconfigdialog.h"
#include "can/pcanadapter.h"

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

    // 按 DPI 自适应窗口尺寸
    qreal dpr = QApplication::primaryScreen()->devicePixelRatio();
    int dlgW = qMax(380, qRound(420 * dpr / 2));
    int dlgH = qMax(260, qRound(300 * dpr / 2));
    setMinimumSize(dlgW, dlgH);
    // 不固定尺寸，允许内容自适应

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // ── 标题 ──
    auto *titleLabel = new QLabel("<b style='font-size:14px; color:#2c3e50;'>会话参数配置</b>");
    layout->addWidget(titleLabel);

    // ── 表单 ──
    auto *form = new QFormLayout();
    form->setSpacing(10);
    form->setContentsMargins(10, 5, 10, 5);

    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(200);

    auto *refreshBtn = new QPushButton("刷新");
    refreshBtn->setFixedWidth(55);
    connect(refreshBtn, &QPushButton::clicked, this, &SessionConfigDialog::scanDevices);

    auto *deviceLayout = new QHBoxLayout();
    deviceLayout->addWidget(m_deviceCombo);
    deviceLayout->addWidget(refreshBtn);
    form->addRow("CAN 设备:", deviceLayout);

    m_baudCombo = new QComboBox();
    m_baudCombo->addItems({"1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K", "5K"});
    m_baudCombo->setCurrentText("500K");
    form->addRow("波特率:", m_baudCombo);

    m_canFdChk = new QCheckBox("启用 CAN-FD (需设备支持)");
    form->addRow("", m_canFdChk);

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
        "border-radius: 3px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #2980b9; }");
    m_buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");

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

    PcanAdapter adapter;
    QList<CanDeviceInfo> devices = adapter.scanDevices();

    if (devices.isEmpty()) {
        m_deviceCombo->addItem("未检测到设备 — 请确保驱动已安装", -1);
        m_statusLabel->setText("⚠ 未检测到 PCAN 设备，请检查连接和驱动");
    } else {
        for (const auto &dev : devices) {
            m_deviceCombo->addItem(QString("%1  [通道 0x%2]")
                .arg(dev.name)
                .arg(dev.channel, 2, 16, QChar('0')), dev.channel);
        }
        m_statusLabel->setText(QString("✓ 检测到 %1 个设备").arg(devices.size()));
    }

    int idx = m_deviceCombo->findText(current, Qt::MatchStartsWith);
    if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);

    // 有设备时自动启用 OK 按钮
    if (auto *btn = m_buttonBox->button(QDialogButtonBox::Ok))
        btn->setEnabled(m_deviceCombo->currentData().toInt() > 0);
}

bool SessionConfigDialog::configure(int &channel, CanBaudRate &baud, bool &isCanFd)
{
    if (exec() != QDialog::Accepted)
        return false;

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
    return true;
}
