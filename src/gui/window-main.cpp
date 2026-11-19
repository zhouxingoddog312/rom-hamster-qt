#include "window-main.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

#include <QApplication>

#include "import-job.h"
#include "model-games.h"
#include "rh-version.h"
#include "window-detail.h"

MainWindow::MainWindow(RhStore *store, const QString &dbPath, const QString &root,
                       RhPlatformRules *rules, QWidget *parent)
    : QMainWindow(parent),
      m_store(store),
      m_dbPath(dbPath),
      m_root(root),
      m_rules(rules)
{
    setWindowTitle(QStringLiteral("rom-hamster — %1").arg(QDir::toNativeSeparators(root)));
    resize(1240, 800);
    setAcceptDrops(true);

    m_readOnly = store_is_read_only(m_store);

    m_model = new GameTableModel(m_store, this);

    buildActions();
    buildToolBar();

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setSortingEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setCentralWidget(m_table);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(
        new QLabel(QStringLiteral("库：%1").arg(QDir::toNativeSeparators(root)), this));

    if (m_readOnly) {
        m_importAction->setEnabled(false);
        statusBar()->showMessage(
            QStringLiteral("库由更高版本的 rom-hamster 创建，当前以只读方式打开。"));
    }

    connect(m_table, &QTableView::doubleClicked, this,
            [this](const QModelIndex &) { openCurrentDetail(); });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) {
                m_detailAction->setEnabled(current.isValid());
            });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateStatus);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::updateStatus);
    connect(m_model, &GameTableModel::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(QStringLiteral("检索失败：%1").arg(message), 8000);
    });

    m_detailAction->setEnabled(false);
    m_model->reload();
    refreshPlatforms();
    updateStatus();
}

void MainWindow::buildActions()
{
    m_importAction = new QAction(QStringLiteral("导入目录…"), this);
    m_importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(m_importAction, &QAction::triggered, this, &MainWindow::chooseAndImport);

    m_refreshAction = new QAction(QStringLiteral("刷新列表"), this);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    connect(m_refreshAction, &QAction::triggered, this, [this]() {
        m_model->reload();
        refreshPlatforms();
        updateStatus();
    });

    QAction *openDir = new QAction(QStringLiteral("打开库目录"), this);
    connect(openDir, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));
    });

    QAction *quit = new QAction(QStringLiteral("退出"), this);
    quit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quit, &QAction::triggered, this, &QWidget::close);

    m_detailAction = new QAction(QStringLiteral("查看详情"), this);
    m_detailAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    connect(m_detailAction, &QAction::triggered, this, &MainWindow::openCurrentDetail);
    addAction(m_detailAction);

    QAction *about = new QAction(QStringLiteral("关于"), this);
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("关于 rom-hamster"),
            QStringLiteral("<b>rom-hamster</b> %1<br/>双端 ROM 库管理器<br/><br/>"
                           "仓库：<a href=\"%2\">%2</a><br/>问题反馈：<a href=\"%3\">%3</a>")
                .arg(QString::fromLatin1(RH_VERSION),
                     QString::fromLatin1(RH_REPO_URL),
                     QString::fromLatin1(RH_ISSUES_URL)));
    });

    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    fileMenu->addAction(m_importAction);
    fileMenu->addAction(m_refreshAction);
    fileMenu->addAction(openDir);
    fileMenu->addSeparator();
    fileMenu->addAction(quit);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("查看"));
    viewMenu->addAction(m_detailAction);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    helpMenu->addAction(about);
}

void MainWindow::buildToolBar()
{
    QToolBar *bar = addToolBar(QStringLiteral("主工具栏"));
    bar->setMovable(false);
    bar->addAction(m_importAction);
    bar->addAction(m_refreshAction);
    bar->addSeparator();

    bar->addWidget(new QLabel(QStringLiteral(" 搜索 "), bar));
    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(QStringLiteral("名称子串…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(260);
    bar->addWidget(m_search);

    bar->addWidget(new QLabel(QStringLiteral(" 平台 "), bar));
    m_platformBox = new QComboBox(bar);
    m_platformBox->setMinimumWidth(170);
    bar->addWidget(m_platformBox);

    bar->addWidget(new QLabel(QStringLiteral(" 排序 "), bar));
    m_sortBox = new QComboBox(bar);
    m_sortBox->addItem(QStringLiteral("名称"), QStringLiteral("name"));
    m_sortBox->addItem(QStringLiteral("评分"), QStringLiteral("rating"));
    m_sortBox->addItem(QStringLiteral("年份"), QStringLiteral("year"));
    m_sortBox->addItem(QStringLiteral("碟数"), QStringLiteral("discs"));
    bar->addWidget(m_sortBox);

    m_descCheck = new QCheckBox(QStringLiteral("降序"), bar);
    bar->addWidget(m_descCheck);

    /* 关键字输入停顿 200 ms 后再查，避免每敲一个字都跑一次 SQL */
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(200);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        m_model->setKeyword(m_search->text().trimmed());
        updateStatus();
    });
    connect(m_search, &QLineEdit::textChanged, this,
            [this](const QString &) { m_searchTimer->start(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        m_searchTimer->stop();
        m_model->setKeyword(m_search->text().trimmed());
        updateStatus();
    });

    connect(m_platformBox, &QComboBox::currentIndexChanged, this, [this](int) {
        m_model->setPlatform(m_platformBox->currentData().toString());
        updateStatus();
    });
    connect(m_sortBox, &QComboBox::currentIndexChanged, this, [this](int) {
        m_model->setSort(m_sortBox->currentData().toString(), m_descCheck->isChecked());
        updateStatus();
    });
    connect(m_descCheck, &QCheckBox::toggled, this, [this](bool) {
        m_model->setSort(m_sortBox->currentData().toString(), m_descCheck->isChecked());
        updateStatus();
    });
}

void MainWindow::refreshPlatforms()
{
    if (m_platformBox == nullptr)
        return;

    const QString current = m_platformBox->currentData().toString();
    m_platformBox->blockSignals(true);
    m_platformBox->clear();
    m_platformBox->addItem(QStringLiteral("全部平台"), QString());

    RhErr err = RH_OK;
    GPtrArray *stats = store_stats_by_platform(m_store, &err);
    if (stats != nullptr) {
        for (guint i = 0; i < stats->len; i++) {
            const RhPlatformStats *item =
                static_cast<const RhPlatformStats *>(g_ptr_array_index(stats, i));
            m_platformBox->addItem(
                QStringLiteral("%1 (%2)").arg(QString::fromUtf8(item->platform))
                    .arg(item->games),
                QString::fromUtf8(item->platform));
        }
        g_ptr_array_unref(stats);
    }

    const int index = m_platformBox->findData(current);
    m_platformBox->setCurrentIndex(index < 0 ? 0 : index);
    m_platformBox->blockSignals(false);
}

void MainWindow::updateStatus()
{
    if (m_statusLabel == nullptr || m_model == nullptr)
        return;
    m_statusLabel->setText(
        QStringLiteral("共 %1 条 · 已加载 %2").arg(m_model->total()).arg(m_model->rowCount()));
}

void MainWindow::openCurrentDetail()
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid())
        return;
    const RhGame *game = m_model->gameAt(current.row());
    if (game == nullptr || game->id == nullptr)
        return;

    DetailWindow window(m_store, m_root, QString::fromUtf8(game->id), this);
    window.exec();
}

void MainWindow::chooseAndImport()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择要导入的目录"), m_root);
    if (dir.isEmpty())
        return;
    startImport(QStringList{dir});
}

void MainWindow::openImportDirectories(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    startImport(paths);
}

void MainWindow::activateFromSecondInstance(const QStringList &arguments)
{
    showNormal();
    raise();
    activateWindow();

    QStringList dirs;
    for (const QString &argument : arguments) {
        const QFileInfo info(argument);
        if (info.isDir())
            dirs.append(info.absoluteFilePath());
    }
    dirs.removeDuplicates();
    if (!dirs.isEmpty())
        startImport(dirs);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_readOnly || event->mimeData() == nullptr)
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (m_readOnly || event->mimeData() == nullptr)
        return;

    QStringList dirs;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isDir())
            dirs.append(info.absoluteFilePath());
        else if (info.isFile())
            dirs.append(info.absolutePath());
    }
    dirs.removeDuplicates();
    if (!dirs.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("拖入的条目按所在目录扫描，共 %1 个来源目录").arg(dirs.size()), 5000);
        startImport(dirs);
    }
    event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_importing) {
        QMessageBox::information(this, QStringLiteral("导入进行中"),
                                 QStringLiteral("请等待导入完成或先取消，再关闭窗口。"));
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::startImport(const QStringList &directories)
{
    if (m_readOnly) {
        QMessageBox::warning(this, QStringLiteral("只读库"),
                             QStringLiteral("当前库以只读方式打开，不能导入。"));
        return;
    }
    if (m_importing) {
        QMessageBox::information(this, QStringLiteral("导入"),
                                 QStringLiteral("已有导入任务在进行中。"));
        return;
    }
    if (m_rules == nullptr) {
        QMessageBox::warning(this, QStringLiteral("导入"),
                             QStringLiteral("平台规则未加载，无法导入。"));
        return;
    }

    QStringList dirs;
    for (const QString &path : directories) {
        QFileInfo info(path);
        const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        if (QFileInfo(dir).isDir() && !dirs.contains(dir))
            dirs.append(dir);
    }
    if (dirs.isEmpty())
        return;

    m_importing = true;

    ImportScanThread *scan = new ImportScanThread(m_rules, m_root, dirs, this);

    QProgressDialog *pulse = new QProgressDialog(
        QStringLiteral("正在预扫描来源目录…"), QString(), 0, 0, this);
    pulse->setWindowTitle(QStringLiteral("导入"));
    pulse->setWindowModality(Qt::WindowModal);
    pulse->setMinimumDuration(0);
    pulse->setAutoClose(false);
    pulse->setAutoReset(false);
    pulse->setCancelButton(nullptr);
    pulse->show();

    connect(scan, &ImportScanThread::scanned, pulse,
            [pulse](int done, int total, const QString &source) {
                pulse->setLabelText(
                    QStringLiteral("正在预扫描（%1/%2）：%3")
                        .arg(done).arg(total)
                        .arg(QDir::toNativeSeparators(source)));
            });

    connect(scan, &QThread::finished, this, [this, scan, pulse]() {
        pulse->close();
        pulse->deleteLater();

        QVector<ImportSourcePlan> results = scan->results;
        scan->results.clear();
        scan->deleteLater();

        QVector<RhImportPlan *> plans;
        QStringList sources;
        QStringList errors;

        int entries = 0;
        int mediaCount = 0;
        int patchCount = 0;
        int ambiguousEntries = 0;
        int existingTargets = 0;
        guint filesScanned = 0;
        guint skippedUnknown = 0;
        guint skippedMedia = 0;
        guint patchArchives = 0;
        guint mixedArchives = 0;
        guint ambiguousArchives = 0;
        guint guessedNames = 0;
        guint escapedNames = 0;

        for (const ImportSourcePlan &result : results) {
            if (!result.error.isEmpty())
                errors.append(QStringLiteral("%1：%2").arg(
                    QDir::toNativeSeparators(result.source), result.error));
            if (result.plan == nullptr)
                continue;

            plans.append(result.plan);
            sources.append(result.source);

            entries += static_cast<int>(result.plan->entries->len);
            filesScanned += result.plan->files_scanned;
            skippedUnknown += result.plan->skipped_unknown;
            skippedMedia += result.plan->skipped_media;
            patchArchives += result.plan->skipped_patch_archives;
            mixedArchives += result.plan->skipped_mixed_archives;
            ambiguousArchives += result.plan->skipped_ambiguous_archives;
            guessedNames += result.plan->guessed_names;
            escapedNames += result.plan->escaped_names;

            for (guint i = 0; i < result.plan->entries->len; i++) {
                const RhImportPlanEntry *entry =
                    static_cast<const RhImportPlanEntry *>(
                        g_ptr_array_index(result.plan->entries, i));
                mediaCount += static_cast<int>(entry->media->len);
                patchCount += static_cast<int>(entry->patches->len);
                if (entry->platform == nullptr)
                    ambiguousEntries++;
                if (entry->target_exists)
                    existingTargets++;
            }
        }

        const QString detailText =
            QStringLiteral("扫描文件：%1\n"
                           "将导入游戏：%2\n"
                           "其中媒体文件：%3，补丁文件：%4\n"
                           "跳过：未知类型 %5，顶层媒体 %6，纯补丁包 %7，"
                           "混装包 %8，平台不唯一包 %9\n"
                           "平台未定（执行时跳过）：%10\n"
                           "目标目录已存在（执行时跳过）：%11\n"
                           "编码推测名：%12，转义名：%13")
                .arg(filesScanned)
                .arg(entries)
                .arg(mediaCount)
                .arg(patchCount)
                .arg(skippedUnknown)
                .arg(skippedMedia)
                .arg(patchArchives)
                .arg(mixedArchives)
                .arg(ambiguousArchives)
                .arg(ambiguousEntries)
                .arg(existingTargets)
                .arg(guessedNames)
                .arg(escapedNames);

        if (entries == 0) {
            rh_import_plans_free(plans);
            m_importing = false;

            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle(QStringLiteral("导入"));
            box.setText(QStringLiteral("没有可导入的条目。"));
            box.setDetailedText(detailText +
                                (errors.isEmpty()
                                     ? QString()
                                     : QStringLiteral("\n\n错误：\n") + errors.join('\n')));
            box.exec();
            return;
        }

        QMessageBox confirm(this);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(QStringLiteral("确认导入"));
        confirm.setText(QStringLiteral("将导入 %1 个游戏，是否继续？").arg(entries));
        confirm.setDetailedText(detailText);
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::Yes);
        if (!errors.isEmpty())
            confirm.setInformativeText(
                QStringLiteral("有 %1 个来源目录扫描失败，详见详情。").arg(errors.size()));

        if (confirm.exec() != QMessageBox::Yes) {
            rh_import_plans_free(plans);
            m_importing = false;
            return;
        }

        gint64 jobId = 0;
        RhErr jobErr = store_job_create(m_store, "import", entries, &jobId);
        if (jobErr != RH_OK)
            jobId = 0;

        ImportExecThread *exec = new ImportExecThread(
            m_rules, m_dbPath, m_root, sources, plans,
            jobId, true, true, this);

        QProgressDialog *progress = new QProgressDialog(
            QStringLiteral("正在导入…"), QStringLiteral("取消"), 0, entries, this);
        progress->setWindowTitle(QStringLiteral("导入"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
        progress->show();

        connect(exec, &ImportExecThread::progressChanged, progress,
                [progress](int done, int total, const QString &label) {
                    progress->setMaximum(total > 0 ? total : 1);
                    progress->setValue(done);
                    if (!label.isEmpty())
                        progress->setLabelText(QStringLiteral("正在导入：%1").arg(label));
                });
        connect(progress, &QProgressDialog::canceled, exec, [exec, progress]() {
            exec->cancel();
            progress->setLabelText(QStringLiteral("正在取消…"));
            progress->setCancelButton(nullptr);
        });

        connect(exec, &QThread::finished, this, [this, exec, progress, jobId]() {
            progress->close();
            progress->deleteLater();

            const RhImportExecStats stats = exec->stats;
            const bool failed = exec->failed;
            const QString error = exec->error;
            exec->deleteLater();

            QString finishError;
            if (failed)
                finishError = error;
            else if (stats.cancelled)
                finishError = QStringLiteral("用户取消");

            if (jobId > 0) {
                const QByteArray utf8 = finishError.toUtf8();
                store_job_finish(m_store, jobId,
                                 finishError.isEmpty() ? nullptr : utf8.constData());
            }

            m_importing = false;
            m_model->reload();
            refreshPlatforms();
            updateStatus();

            QString text = QStringLiteral("导入完成：成功 %1，跳过 %2，失败 %3。")
                               .arg(stats.imported).arg(stats.skipped).arg(stats.failed);
            if (stats.cancelled)
                text = QStringLiteral("导入已取消。") + text;
            else if (failed)
                text += QStringLiteral("\n错误：%1").arg(error);
            QMessageBox::information(this, QStringLiteral("导入"), text);
        });

        exec->start();
    });

    scan->start();
}
