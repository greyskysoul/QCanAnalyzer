#include "canmanager.h"
#ifndef Q_OS_LINUX
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#endif
#include "can/zcanfdadapter.h"
#ifndef Q_OS_LINUX
#include "can/zcanadapter.h"
#endif
#include "can/socketcanadapter.h"
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>
#include <QDebug>

CanManager::CanManager(ads::CDockManager *dockManager, QObject *parent)
    : QObject(parent)
    , m_dockManager(dockManager)
{
}

CanManager::~CanManager()
{
    QList<int> ids = m_sessions.keys();
    for (int id : ids)
        closeSession(id);
}

CanSessionWidget *CanManager::createSession(int channel, CanBaudRate baud,
                                            bool isCanFd, int adapterType,
                                            const QString &deviceName,
                                            CanDataBaudRate dataBaud)
{
    int id = m_nextSessionId++;

    auto *widget = new CanSessionWidget(id);
    widget->setCanFdEnabled(isCanFd);
    widget->setBaudRateText(baudRateString(baud));
    widget->setDataBaudRate(dataBaud);
    m_sessions[id] = widget;

    // 配置对话框选定的设备名优先，否则按适配器类型生成
    QString devName = deviceName;
    if (devName.isEmpty()) {
        switch (static_cast<CanAdapterType>(adapterType)) {
#ifndef Q_OS_LINUX
        case CanAdapterType::PCAN:  devName = PcanAdapter::channelName(channel); break;
        case CanAdapterType::GsUsb: devName = GsUsbAdapter::channelName(channel); break;
#endif
        case CanAdapterType::ZCANFD: devName = ZcanFdAdapter::channelName(channel); break;
#ifndef Q_OS_LINUX
        case CanAdapterType::ZCAN:  devName = ZcanAdapter::channelName(channel); break;
#endif
#ifdef Q_OS_LINUX
        case CanAdapterType::SocketCAN: devName = "can0"; break;
#endif
        default: devName = QString("CAN-%1").arg(channel); break;
        }
    }

    auto *dockWidget = new ads::CDockWidget(m_dockManager, devName);
    dockWidget->setWidget(widget);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    m_dockWidgets[id] = dockWidget;

    // 所有会话共用同一个标签组
    if (m_sessions.size() == 1)
        m_lastArea = m_dockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget);
    else
        addToExistingTabGroup(dockWidget);

    connect(dockWidget, &ads::CDockWidget::closed, this, [this, id]() {
        if (!m_closingSessions.contains(id))
            closeSession(id);
    });

    // 兜底：closeSession 未触发时（如父窗口直接析构）仍清理 dock
    connect(widget, &QObject::destroyed, this, [this, id]() {
        m_closingSessions.remove(id);
        if (m_dockWidgets.contains(id)) {
            m_dockWidgets[id]->deleteLater();
            m_dockWidgets.remove(id);
        }
        if (m_sessions.isEmpty()) {
            m_lastArea = nullptr;
            emit allSessionsClosed();
        }
    });

    // 用 widget 归一化后的值（非 FD 时为 None），与会话内重连路径保持一致
    widget->connectDevice(channel, baud, adapterType, widget->dataBaudRate());

    emit sessionCreated(id);
    return widget;
}

void CanManager::addToExistingTabGroup(ads::CDockWidget *dockWidget)
{
    if (!findValidArea()) {
        m_lastArea = m_dockManager->addDockWidget(
            ads::CenterDockWidgetArea, dockWidget);
        return;
    }

    m_dockManager->addDockWidgetTabToArea(dockWidget, m_lastArea);
}

bool CanManager::findValidArea()
{
    if (!m_lastArea || !m_dockManager) return false;

    QList<ads::CDockAreaWidget*> areas = m_dockManager->openedDockAreas();
    if (!areas.contains(m_lastArea))
        m_lastArea = areas.isEmpty() ? nullptr : areas.first();
    return m_lastArea != nullptr;
}

void CanManager::closeSession(int sessionId)
{
    if (m_closingSessions.contains(sessionId)) return;
    m_closingSessions.insert(sessionId);

    if (m_sessions.contains(sessionId)) {
        // 不调 disconnectDevice(): 会话可能由 dock 关闭触发销毁，此时 UI 状态不确定。
        // 适配器由 CanSessionWidget 析构函数负责关闭。
        CanSessionWidget *w = m_sessions.take(sessionId);
        w->deleteLater();
    }

    if (m_dockWidgets.contains(sessionId)) {
        ads::CDockWidget *dw = m_dockWidgets.take(sessionId);
        dw->deleteLater();
    }

    emit sessionClosed(sessionId);

    if (m_sessions.isEmpty()) {
        m_lastArea = nullptr;
        emit allSessionsClosed();
    }
}

QList<CanSessionWidget*> CanManager::sessions() const
{
    return m_sessions.values();
}

int CanManager::sessionCount() const
{
    return m_sessions.size();
}

bool CanManager::hasSessions() const
{
    return !m_sessions.isEmpty();
}
