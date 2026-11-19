#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include "core-bridge.h"
#include "rh-version.h"
#include "version-check.h"
#include "window-main.h"

#ifndef RH_UPDATE_CHECK_URL
#define RH_UPDATE_CHECK_URL ""
#endif

#ifndef RH_DATA_DIR
#define RH_DATA_DIR ""
#endif

namespace {

/*
 * 图标来自 Qt 资源（resources/resources.qrc 的 /icons 前缀）。
 * 逐尺寸加入 QIcon，避免依赖 QtSvg（§8.3 的依赖清单里没有它）。
 */
QIcon applicationIcon()
{
    QIcon icon;
    const int sizes[] = { 16, 32, 48, 256 };
    for (int size : sizes) {
        icon.addFile(QStringLiteral(":/icons/rom-hamster-%1.png").arg(size),
                     QSize(size, size));
    }
    return icon;
}

/* 数据文件（platforms.conf）查找：环境变量 → 安装布局 → 源码树 */
QString findDataFile(const QString &name)
{
    QStringList candidates;
    const QString env = qEnvironmentVariable("ROM_HAMSTER_DATA_DIR");
    if (!env.isEmpty())
        candidates << QDir(env).filePath(name);
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir + QStringLiteral("/../share/rom-hamster")).filePath(name)
               << QDir(appDir + QStringLiteral("/share/rom-hamster")).filePath(name)
               << QDir(appDir + QStringLiteral("/../../share/rom-hamster")).filePath(name)
               << QDir(QString::fromUtf8(RH_DATA_DIR)).filePath(name)
               << QDir(QStringLiteral("/usr/share/rom-hamster")).filePath(name);

    for (const QString &candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return QString();
}

QString singleInstanceName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = QStringLiteral("default");
    return QStringLiteral("rom-hamster-gui-%1").arg(user);
}

} // namespace

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
    QCommandLineOption dbOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QStringLiteral("数据库文件（默认 <库根目录>/romhamster.db）"), QStringLiteral("PATH"));
    QCommandLineOption rootOption(
        {QStringLiteral("r"), QStringLiteral("root")},
        QStringLiteral("ROM 库根目录"), QStringLiteral("DIR"));
    QCommandLineOption importOption(
        QStringLiteral("import"), QStringLiteral("启动后导入目录"), QStringLiteral("DIR"));
    parser.addOption(dbOption);
    parser.addOption(rootOption);
    parser.addOption(importOption);
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    const QStringList importDirs =
        parser.values(importOption) + positional;

    /* --- 单实例：第二次启动只激活已有窗口并把参数转过去 --- */
    const QString serverName = singleInstanceName();
    QLocalSocket probe;
    probe.connectToServer(serverName);
    if (probe.waitForConnected(300)) {
        const QStringList forwarded =
            parser.values(importOption) + parser.positionalArguments();
        probe.write(forwarded.join(QLatin1Char('\n')).toUtf8());
        probe.flush();
        probe.waitForBytesWritten(500);
        probe.disconnectFromServer();
        return 0;
    }

    QLocalServer::removeServer(serverName);
    QLocalServer server;

    /* --- 库根目录：命令行 → 上次使用 → 首启选择 --- */
    QString root = parser.value(rootOption);
    QString db = parser.value(dbOption);
    QSettings settings;

    if (db.isEmpty()) {
        if (root.isEmpty())
            root = settings.value(QStringLiteral("library/root")).toString();
        if (root.isEmpty() || !QFileInfo(root).isDir()) {
            const QString picked = QFileDialog::getExistingDirectory(
                nullptr, QStringLiteral("选择 ROM 库根目录"),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
            if (picked.isEmpty())
                return 0;
            root = picked;
        }
        if (!QFileInfo(root).isDir() && !QDir().mkpath(root)) {
            QMessageBox::critical(nullptr, QStringLiteral("rom-hamster"),
                                  QStringLiteral("无法创建库根目录：%1").arg(root));
            return 1;
        }
        db = QDir(root).filePath(QStringLiteral("romhamster.db"));
        settings.setValue(QStringLiteral("library/root"), root);
    } else if (root.isEmpty()) {
        root = QFileInfo(db).absolutePath();
    }

    RhErr err = RH_OK;
    RhStore *store = store_open(db.toUtf8().constData(), &err);
    if (store == nullptr) {
        QMessageBox::critical(
            nullptr, QStringLiteral("无法打开数据库"),
            QStringLiteral("%1\n%2").arg(QDir::toNativeSeparators(db),
                                         QString::fromUtf8(rh_err_str(err))));
        return 1;
    }

    const bool readOnly = store_is_read_only(store);

    /* --- 平台规则（缺失时仍可浏览，导入会被禁用） --- */
    RhPlatformRules *rules = nullptr;
    const QString rulesPath = findDataFile(QStringLiteral("platforms.conf"));
    if (!rulesPath.isEmpty()) {
        gchar *message = nullptr;
        rules = rh_platform_rules_load(rulesPath.toUtf8().constData(), &err, &message);
        if (rules == nullptr && message != nullptr)
            qWarning("platforms.conf: %s", message);
        g_free(message);
    } else {
        qWarning("未找到 platforms.conf，导入功能不可用");
    }

    RhCtx *ctx = rh_ctx_new();
    rh_ctx_set_rom_root(ctx, root.toUtf8().constData());
    rh_ctx_set_store_path(ctx, db.toUtf8().constData());

    MainWindow window(store, db, root, rules);
    window.show();

    if (readOnly) {
        QMessageBox::warning(
            &window, QStringLiteral("只读库"),
            QStringLiteral("该库由更高版本的 rom-hamster 创建，当前以只读方式打开。\n"
                           "请升级 rom-hamster 后再写入。"));
    }

    if (!importDirs.isEmpty())
        window.openImportDirectories(importDirs);

    QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, &window]() {
        QLocalSocket *connection = server.nextPendingConnection();
        if (connection == nullptr)
            return;
        QObject::connect(connection, &QLocalSocket::disconnected,
                         connection, &QLocalSocket::deleteLater);
        QObject::connect(connection, &QLocalSocket::readyRead, connection,
                         [connection, &window]() {
                             const QStringList arguments =
                                 QString::fromUtf8(connection->readAll())
                                     .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                             window.activateFromSecondInstance(arguments);
                         });
    });
    if (!server.listen(serverName))
        qWarning("单实例服务启动失败：%s", server.errorString().toUtf8().constData());

    UpdateChecker checker;
    QObject::connect(&checker, &UpdateChecker::updateAvailable, &window,
                     [&window](const QString &version, const QString &url) {
                         QMessageBox *box = new QMessageBox(&window);
                         box->setAttribute(Qt::WA_DeleteOnClose);
                         box->setWindowModality(Qt::NonModal);
                         box->setIcon(QMessageBox::Information);
                         box->setWindowTitle(QStringLiteral("发现新版本"));
                         box->setText(QStringLiteral("发现新版本 %1，是否打开下载页？")
                                          .arg(version));
                         box->setStandardButtons(QMessageBox::Open | QMessageBox::Close);
                         QObject::connect(box, &QMessageBox::finished, box,
                                          [url](int result) {
                                              if (result == QMessageBox::Open)
                                                  QDesktopServices::openUrl(QUrl(url));
                                          });
                         box->show();
                     });
    checker.check(QString::fromLatin1(RH_UPDATE_CHECK_URL), 24);

    const int code = app.exec();

    server.close();
    rh_ctx_free(ctx);
    if (rules != nullptr)
        rh_platform_rules_free(rules);
    store_close(store);
    return code;
}
