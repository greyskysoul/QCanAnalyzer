#ifndef CANMANAGER_H
#define CANMANAGER_H

#include "ui/cansessionwidget.h"
#include <QObject>
#include <QMap>
#include <QSet>

namespace ads {
    class CDockManager;
    class CDockWidget;
    class CDockAreaWidget;
}

/// CAN 会话管理器 —— 只负责会话与 dock 标签组的生命周期，不持有适配器
class CanManager : public QObject
{
    Q_OBJECT

public:
    explicit CanManager(ads::CDockManager *dockManager, QObject *parent = nullptr);
    ~CanManager() override;

    CanSessionWidget *createSession(int channel, CanBaudRate baud,
                                    bool isCanFd = false, int adapterType = 0,
                                    const QString &deviceName = {},
                                    CanDataBaudRate dataBaud = CanDataBaudRate::None);
    void closeSession(int sessionId);

    QList<CanSessionWidget*> sessions() const;
    int sessionCount() const;
    bool hasSessions() const;

signals:
    void sessionCreated(int sessionId);
    void sessionClosed(int sessionId);
    void allSessionsClosed();

private:
    void addToExistingTabGroup(ads::CDockWidget *dockWidget);
    bool findValidArea();

    ads::CDockManager   *m_dockManager;
    ads::CDockAreaWidget *m_lastArea = nullptr; // 上一个会话所在的标签区域
    QMap<int, CanSessionWidget*> m_sessions;
    QMap<int, ads::CDockWidget*> m_dockWidgets;
    QSet<int> m_closingSessions; // 防重入
    int m_nextSessionId = 1;
};

#endif // CANMANAGER_H
