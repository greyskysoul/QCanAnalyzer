#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>
#include <QIcon>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    a.setWindowIcon(QIcon(":/icon.png"));
    a.setStyle(QStyleFactory::create("Fusion"));

    // 全局字体略大一号，保证可读性
    QFont font = a.font();
    font.setPointSize(9);
    a.setFont(font);

    MainWindow w;
    w.show();
    return a.exec();
}
