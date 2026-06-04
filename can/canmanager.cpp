#include "canmanager.h"
#ifndef Q_OS_LINUX
#include "can/pcanadapter.h"
#include "can/gsusbadapter.h"
#else
#include "can/socketcanadapter.h"
#endif
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
                                            bool isCanFd, int adapterType)
{
    Q_UNUSED(isCanFd)

    int id = m_nextSessionId++;

    auto *widget = new CanSessionWidget(id);
    m_sessions[id] = widget;

    // 标签名根据适配器类型
    QString devName;
    switch (static_cast<CanAdapterType>(adapterType)) {
#ifndef Q_OS_LINUX
    case CanAdapterType::PCAN:     devName = PcanAdapter::channelName(channel); break;
    case CanAdapterType::GsUsb:    devName = GsUsbAdapter::channelName(channel); break;
#endif
#ifdef Q_OS_LINUX
    case CanAdapterType::SocketCAN:devName = SocketCanAdapter::channelName(channel); break;
#endif
    default: devName = QString("CAN-%1").arg(channel);
    }
    QString title = QString("%1 @ %2").arg(devName).arg(baudRateString(baud));

    // 创建停靠窗口
    auto *dockWidget = new ads::CDockWidget(m_dockManager, title);
    dockWidget->setWidget(widget);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    m_dockWidgets[id] = dockWidget;

    // ── 添加到同一标签组 ──
    if (m_sessions.size() == 1) {
        // 第一个会话: 正常添加
        m_lastArea = m_dockManager->addDockWidget(
            ads::CenterDockWidgetArea, dockWidget);
    } else {
        // 后续会话: 加入已有标签组
        addToExistingTabGroup(dockWidget);
    }

    // 关闭时清理 (使用 closing 集合防止重复触发)
    connect(dockWidget, &ads::CDockWidget::closed, this, [this, id]() {
        if (!m_closingSessions.contains(id))
            closeSession(id);
    });

    // 会话销毁时确保dock也被清理 (仅当 closeSession 未正常触发时作为兜底)
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

    // 自动连接设备
    widget->connectDevice(channel, baud, adapterType);

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

    // 使用 m_lastArea 作为标签组目标区域
    m_dockManager->addDockWidgetTabToArea(dockWidget, m_lastArea);
}

bool CanManager::findValidArea()
{
    // 检查 m_lastArea 是否仍然有效
    if (!m_lastArea || !m_dockManager) return false;

    QList<ads::CDockAreaWidget*> areas = m_dockManager->openedDockAreas();
    if (!areas.contains(m_lastArea)) {
        // m_lastArea 已失效，尝试从现有 areas 中找一个
        m_lastArea = areas.isEmpty() ? nullptr : areas.first();
    }
    return m_lastArea != nullptr;
}

void CanManager::closeSession(int sessionId)
{
    // 防止重复关闭
    if (m_closingSessions.contains(sessionId)) return;
    m_closingSessions.insert(sessionId);

    if (m_sessions.contains(sessionId)) {
        CanSessionWidget *w = m_sessions.take(sessionId);
        // 不在此处调用 disconnectDevice(), 而是直接 deleteLater
        // 因为 disconnectDevice() 访问 UI 控件，而 closeSession 可能由 dock 关闭触发
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
