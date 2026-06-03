#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QStackedWidget;
namespace ads { class CDockManager; }
class CanManager;
class WelcomeWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onNewSession();
    void onCloseAllSessions();
    void onAllSessionsClosed();

private:
    void setupMenuBar();
    void setupStatusBar();
    void showWelcomePage();
    void hideWelcomePage();

    Ui::MainWindow *ui;
    QStackedWidget *m_stack = nullptr;
    ads::CDockManager *m_dockManager = nullptr;
    CanManager *m_canManager = nullptr;
    WelcomeWidget *m_welcomeWidget = nullptr;
};
#endif // MAINWINDOW_H
