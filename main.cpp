#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QFile>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    a.setWindowIcon(QIcon(":/icon.png"));
    a.setStyle(QStyleFactory::create("Fusion"));

    // 全局字体 —— 可读性优先
    QFont font;
#ifdef Q_OS_WIN
    font = QFont("Segoe UI", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
#else
    font = QFont("Noto Sans", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
#endif
    font.setHintingPreference(QFont::PreferFullHinting);
    a.setFont(font);

    // 加载全局样式表
    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }

    MainWindow w;
    w.show();
    return a.exec();
}
