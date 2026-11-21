#include "mainwindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("XiangqiAnalyzer"));
    QApplication::setApplicationName(QStringLiteral("棋析"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QFont font(QStringLiteral("PingFang SC"));
    font.setPointSize(13);
    QApplication::setFont(font);

    MainWindow window;
    window.show();
    return QApplication::exec();
}
