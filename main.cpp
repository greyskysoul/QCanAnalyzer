#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QFile>
#include <QLocale>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    a.setWindowIcon(QIcon(":/icon.png"));
    a.setStyle(QStyleFactory::create("Fusion"));

    // 全局字体
    QFont font;
#ifdef Q_OS_WIN
    font = QFont("Segoe UI", 10);
#else
    font = QFont("Noto Sans", 10);
#endif
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    a.setFont(font);

    // 语言优先级: --lang 参数 > 系统语言
    // 用法: QCanAnalyzer.exe --lang en_US
    QString lang;
    QStringList args = a.arguments();
    int langIdx = args.indexOf("--lang");
    if (langIdx >= 0 && langIdx + 1 < args.size()) {
        lang = args[langIdx + 1];
        args.removeAt(langIdx);
        args.removeAt(langIdx);
    }
    if (lang.isEmpty())
        lang = QLocale::system().name(); // 如 "zh_CN", "en_US"

    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }

    MainWindow w(lang);
    w.show();
    return a.exec();
}
