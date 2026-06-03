#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    // ─── Qt 高 DPI 自适应（Qt 5.6+）───
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    // 设置为 Fusion 风格，在高DPI下一致性好
    a.setStyle(QStyleFactory::create("Fusion"));

    // 全局字体略大一号，保证可读性
    QFont font = a.font();
    font.setPointSize(9);
    a.setFont(font);

    MainWindow w;
    w.show();
    return a.exec();
}
