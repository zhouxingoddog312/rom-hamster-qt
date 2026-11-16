#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMainWindow>

#include "rh-version.h"

/*
 * 图标来自 Qt 资源（resources/resources.qrc 的 /icons 前缀）。
 * 逐尺寸加入 QIcon，避免依赖 QtSvg（§8.3 的依赖清单里没有它）。
 */
static QIcon applicationIcon()
{
    QIcon icon;
    const int sizes[] = { 16, 32, 48, 256 };
    for (int size : sizes) {
        icon.addFile(QStringLiteral(":/icons/rom-hamster-%1.png").arg(size),
                     QSize(size, size));
    }
    return icon;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(RH_PROJECT_NAME));
    QApplication::setApplicationDisplayName(QStringLiteral("rom-hamster"));
    QApplication::setApplicationVersion(QStringLiteral(RH_VERSION));
    QApplication::setOrganizationName(QStringLiteral("goddog312"));
    QApplication::setWindowIcon(applicationIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ROM 库管理器"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    /* M0：空窗口；主窗口/详情/导入在 M4 落地 */
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("rom-hamster"));
    window.resize(1200, 760);
    window.show();

    return app.exec();
}
