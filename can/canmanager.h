#ifndef CANMANAGER_H
#define CANMANAGER_H

#include "ui/cansessionwidget.h"
#include <QObject>
#include <QMap>

namespace ads {
    class CDockManager;
    class CDockWidget;
    class CDockAreaWidget;
}

/// CAN 会话管理器 —— 管理多个 CAN 会话的生命周期
class CanManager : public QObject
{
    Q_OBJECT

public:
    explicit CanManager(ads::CDockManager *dockManager, QObject *parent = nullptr);
    ~CanManager() override;

    /// 创建新会话 (带配置参数)
    CanSessionWidget *createSession(int channel, CanBaudRate baud,
                                    bool isCanFd = false, int adapterType = 0);

    /// 关闭指定会话
    void closeSession(int sessionId);

    /// 获取会话列表
    QList<CanSessionWidget*> sessions() const;

    /// 会话数量
    int sessionCount() const;

    /// 是否有活跃会话
    bool hasSessions() const;

    /// 获取停靠管理器 (供 MainWindow 使用)
    ads::CDockManager *dockManager() const { return m_dockManager; }

signals:
    void sessionCreated(int sessionId);
    void sessionClosed(int sessionId);
    void allSessionsClosed();

private:
    void addToExistingTabGroup(ads::CDockWidget *dockWidget);

    ads::CDockManager   *m_dockManager;
    ads::CDockAreaWidget *m_lastArea = nullptr; // 上一个会话所在的标签区域
    QMap<int, CanSessionWidget*> m_sessions;
    QMap<int, ads::CDockWidget*> m_dockWidgets;
    int m_nextSessionId = 1;
};

#endif // CANMANAGER_H
