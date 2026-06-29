#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTranslator>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QStackedWidget;
class QActionGroup;
namespace ads { class CDockManager; }
class CanManager;
class WelcomeWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const QString &initialLang, QWidget *parent = nullptr);
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
    void initLanguage(const QString &lang);
    void switchLanguage(const QString &lang);

    Ui::MainWindow *ui;
    QStackedWidget *m_stack = nullptr;
    ads::CDockManager *m_dockManager = nullptr;
    CanManager *m_canManager = nullptr;
    WelcomeWidget *m_welcomeWidget = nullptr;

    // ─── 翻译 ───
    QString      m_currentLang;
    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;

    // ─── 语言菜单 ───
    QActionGroup *m_langGroup = nullptr;

    // ─── 需要动态更新的 UI 字符串 ───
    QString m_statusReadyMsg;
};
#endif // MAINWINDOW_H
