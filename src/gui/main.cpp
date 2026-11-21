#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>
#include <QUrl>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core-bridge.h"
#include "rh-version.h"
#include "version-check.h"
#include "window-main.h"

#ifndef _WIN32
#include <unistd.h>
#endif

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

/*
 * Windows 的 GUI 子系统没有控制台，Qt 的 --version 会退化成消息框并阻塞；
 * 打包自检需要在无窗口、无交互的情况下拿到退出码，所以先直接输出。
 */
bool printVersionIfRequested(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--version") == 0
            || std::strcmp(argv[i], "-v") == 0) {
            std::printf("%s %s\n", RH_PROJECT_NAME, RH_VERSION);
            return true;
        }
    }
    return false;
}

/*
 * GUI 翻译（§11）：.qm 由 CMake 的 lrelease 规则编进资源 :/i18n。
 * 加载失败（没有对应语言的 .qm，或构建时没装 lrelease）时保持源码字符串，
 * 不产生任何提示。
 */
void installTranslator(QApplication &app)
{
    QTranslator *translator = new QTranslator(&app);
    const QString locale = QLocale::system().name();
    const QStringList candidates = {
        locale,
        locale.section(QLatin1Char('_'), 0, 0),
    };
    for (const QString &name : candidates) {
        if (translator->load(QStringLiteral(":/i18n/rom-hamster_%1.qm").arg(name))) {
            app.installTranslator(translator);
            return;
        }
    }
    delete translator;
}

/*
 * AppImage 的应急 CLI 入口（§9.3）：rom-hamster.AppImage --cli … 交给同目录的
 * rom-hamster。图形会话损坏时仍能查库；找不到 CLI 时照常按 GUI 启动。
 */
#ifndef _WIN32
bool runCliFallback(int argc, char *argv[])
{
    if (argc < 2 || std::strcmp(argv[1], "--cli") != 0)
        return false;
    // QApplication 还没创建，不能问 applicationDirPath()；读 /proc/self/exe
    const QString self = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    const QString cli = QFileInfo(self).dir().filePath(QStringLiteral("rom-hamster"));
    if (self.isEmpty() || !QFileInfo::exists(cli))
        return false;

    std::vector<std::string> storage;
    storage.emplace_back(QFile::encodeName(cli).constData());
    for (int i = 2; i < argc; ++i)
        storage.emplace_back(argv[i]);
    std::vector<char *> args;
    for (std::string &item : storage)
        args.push_back(const_cast<char *>(item.c_str()));
    args.push_back(nullptr);

    execv(args[0], args.data());
    return false; // execv 失败：按 GUI 继续启动
}
#endif

} // namespace

int main(int argc, char *argv[])
{
#ifndef _WIN32
    if (runCliFallback(argc, argv))
        return 0;
#endif
    if (printVersionIfRequested(argc, argv))
        return 0;

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(RH_PROJECT_NAME));
    QApplication::setApplicationDisplayName(QStringLiteral("rom-hamster"));
    QApplication::setApplicationVersion(QStringLiteral(RH_VERSION));
    QApplication::setOrganizationName(QStringLiteral("goddog312"));
    QApplication::setWindowIcon(applicationIcon());
    installTranslator(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("rom-hamster", "ROM Library Manager"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dbOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QCoreApplication::translate(
            "rom-hamster", "Database file (default: <library root>/romhamster.db)"),
        QStringLiteral("PATH"));
    QCommandLineOption rootOption(
        {QStringLiteral("r"), QStringLiteral("root")},
        QCoreApplication::translate("rom-hamster", "ROM library root directory"),
        QStringLiteral("DIR"));
    QCommandLineOption importOption(
        QStringLiteral("import"),
        QCoreApplication::translate("rom-hamster", "Import directory after startup"),
        QStringLiteral("DIR"));
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
                nullptr,
                QCoreApplication::translate("rom-hamster", "Select ROM Library Root Directory"),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
            if (picked.isEmpty())
                return 0;
            root = picked;
        }
        if (!QFileInfo(root).isDir() && !QDir().mkpath(root)) {
            QMessageBox::critical(
                nullptr, QStringLiteral("rom-hamster"),
                QCoreApplication::translate("rom-hamster", "Unable to create library root directory: %1")
                    .arg(root));
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
            nullptr, QCoreApplication::translate("rom-hamster", "Unable to Open Database"),
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
        qWarning("platforms.conf not found; import is unavailable");
    }

    RhCtx *ctx = rh_ctx_new();
    rh_ctx_set_rom_root(ctx, root.toUtf8().constData());
    rh_ctx_set_store_path(ctx, db.toUtf8().constData());

    MainWindow window(store, db, root, rules);
    window.show();

    if (readOnly) {
        QMessageBox::warning(
            &window, QCoreApplication::translate("rom-hamster", "Read-Only Library"),
            QCoreApplication::translate(
                "rom-hamster",
                "This library was created by a newer version of rom-hamster and is open in read-only mode.\n"
                "Upgrade rom-hamster before writing to it."));
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
        qWarning("Failed to start single-instance server: %s",
                 server.errorString().toUtf8().constData());

    UpdateChecker checker;
    QObject::connect(&checker, &UpdateChecker::updateAvailable, &window,
                     [&window](const QString &version, const QString &url) {
                         QMessageBox *box = new QMessageBox(&window);
                         box->setAttribute(Qt::WA_DeleteOnClose);
                         box->setWindowModality(Qt::NonModal);
                         box->setIcon(QMessageBox::Information);
                         box->setWindowTitle(
                             QCoreApplication::translate("rom-hamster", "New Version Available"));
                         box->setText(QCoreApplication::translate(
                                          "rom-hamster",
                                          "Version %1 is available. Open the download page?")
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
