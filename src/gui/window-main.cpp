#include "window-main.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <QApplication>

#include "dat-dialog.h"
#include "dedup-dialog.h"
#include "export-dialog.h"
#include "import-dups-dialog.h"
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
        new QLabel(tr("Library: %1").arg(QDir::toNativeSeparators(root)), this));

    if (m_readOnly) {
        m_importAction->setEnabled(false);
        m_importFilesAction->setEnabled(false);
        statusBar()->showMessage(
            tr("This library was created by a newer version of rom-hamster and is open in read-only mode."));
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
        statusBar()->showMessage(tr("Query failed: %1").arg(message), 8000);
    });

    m_detailAction->setEnabled(false);
    m_model->reload();
    refreshPlatforms();
    updateStatus();
}

void MainWindow::buildActions()
{
    m_importAction = new QAction(tr("Import Directory…"), this);
    m_importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(m_importAction, &QAction::triggered, this, &MainWindow::chooseAndImport);

    m_importFilesAction = new QAction(tr("Import Files…"), this);
    m_importFilesAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    connect(m_importFilesAction, &QAction::triggered, this,
            &MainWindow::chooseAndImportFiles);

    m_refreshAction = new QAction(tr("Refresh List"), this);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    connect(m_refreshAction, &QAction::triggered, this, [this]() {
        m_model->reload();
        refreshPlatforms();
        updateStatus();
    });

    QAction *openDir = new QAction(tr("Open Library Directory"), this);
    connect(openDir, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));
    });

    QAction *datSources = new QAction(tr("DAT Sources…"), this);
    connect(datSources, &QAction::triggered, this, [this]() {
        DatDialog dialog(m_dbPath, this);
        dialog.exec();
    });

    QAction *findDuplicates = new QAction(tr("Find Duplicates…"), this);
    connect(findDuplicates, &QAction::triggered, this, [this]() {
        DedupDialog dialog(m_dbPath, m_root, this);
        dialog.exec();
    });

    QAction *quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quit, &QAction::triggered, this, &QWidget::close);

    m_detailAction = new QAction(tr("View Details"), this);
    m_detailAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    connect(m_detailAction, &QAction::triggered, this, &MainWindow::openCurrentDetail);
    addAction(m_detailAction);

    QAction *about = new QAction(tr("About rom-hamster…"), this);
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, tr("About rom-hamster"),
            tr("<b>rom-hamster</b> %1<br/>Dual-platform ROM Library Manager<br/><br/>"
               "<b>Workflow</b><br/>"
               "Import Games: bring ROMs, archives, media, and patches into the library.<br/>"
               "Manage Games: search and filter the library, view details, maintain DAT sources, "
               "and find duplicates.<br/>"
               "Export Games: generate front-end manifests (ES-DE, EmulationStation, Pegasus).<br/><br/>"
               "Qt %2<br/>"
               "Repository: <a href=\"%3\">%3</a><br/>Report issues: <a href=\"%4\">%4</a>")
                .arg(QString::fromLatin1(RH_VERSION),
                     QString::fromLatin1(qVersion()),
                     QString::fromLatin1(RH_REPO_URL),
                     QString::fromLatin1(RH_ISSUES_URL)));
    });

    QAction *exportManifest = new QAction(tr("Export Front-end…"), this);
    connect(exportManifest, &QAction::triggered, this, [this]() {
        ExportDialog dialog(m_dbPath, m_root, m_rules, this);
        dialog.exec();
    });

    QMenu *importMenu = menuBar()->addMenu(tr("Import Games"));
    importMenu->addAction(m_importAction);
    importMenu->addAction(m_importFilesAction);

    QMenu *manageMenu = menuBar()->addMenu(tr("Manage Games"));
    manageMenu->addAction(m_refreshAction);
    manageMenu->addAction(m_detailAction);
    manageMenu->addSeparator();
    manageMenu->addAction(findDuplicates);
    manageMenu->addAction(datSources);
    manageMenu->addSeparator();
    manageMenu->addAction(openDir);
    manageMenu->addSeparator();
    manageMenu->addAction(quit);

    QMenu *exportMenu = menuBar()->addMenu(tr("Export Games"));
    exportMenu->addAction(exportManifest);

    QMenu *aboutMenu = menuBar()->addMenu(tr("About"));
    aboutMenu->addAction(about);
}

void MainWindow::buildToolBar()
{
    QToolBar *bar = addToolBar(tr("Main Toolbar"));
    bar->setMovable(false);
    bar->addAction(m_importAction);
    bar->addAction(m_importFilesAction);
    bar->addAction(m_refreshAction);
    bar->addSeparator();

    bar->addWidget(new QLabel(tr(" Search "), bar));
    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Name substring…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(260);
    bar->addWidget(m_search);

    bar->addWidget(new QLabel(tr(" Platform "), bar));
    m_platformBox = new QComboBox(bar);
    m_platformBox->setMinimumWidth(170);
    bar->addWidget(m_platformBox);

    bar->addWidget(new QLabel(tr(" Sort "), bar));
    m_sortBox = new QComboBox(bar);
    m_sortBox->addItem(tr("Name"), QStringLiteral("name"));
    m_sortBox->addItem(tr("Rating"), QStringLiteral("rating"));
    m_sortBox->addItem(tr("Year"), QStringLiteral("year"));
    m_sortBox->addItem(tr("Disc Count"), QStringLiteral("discs"));
    bar->addWidget(m_sortBox);

    m_descCheck = new QCheckBox(tr("Descending"), bar);
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
    m_platformBox->addItem(tr("All Platforms"), QString());

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
        tr("%1 entries · %2 loaded").arg(m_model->total()).arg(m_model->rowCount()));
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
        this, tr("Select Directory to Import"), m_root);
    if (dir.isEmpty())
        return;
    startImport(QStringList{dir});
}

void MainWindow::chooseAndImportFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Files to Import"), m_root,
        tr("ROM files (*.nes *.sfc *.smc *.gb *.gbc *.gba *.md *.gen *.sms "
           "*.gg *.pce *.a26 *.z64 *.n64 *.nds *.3ds *.iso *.bin *.cue *.chd "
           "*.zip *.7z *.rar);;All files (*)"));
    if (files.isEmpty())
        return;
    startImport(files);
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
            tr("Dropped items are scanned by their containing directories: %1 source directories")
                .arg(dirs.size()),
            5000);
        startImport(dirs);
    }
    event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_importing) {
        QMessageBox::information(
            this, tr("Import in Progress"),
            tr("Wait for the import to finish or cancel it before closing the window."));
        event->ignore();
        return;
    }
    event->accept();
}

static QString choosePatchTarget(QWidget *parent, const GPtrArray *games,
                                 const QString &entryName, const QString &source)
{
    if (games == nullptr || games->len == 0)
        return QString();

    QDialog dialog(parent);
    dialog.setWindowTitle(
        QCoreApplication::translate("MainWindow", "Assign Patch-Only Archive to Game"));
    dialog.resize(560, 220);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QCoreApplication::translate(
            "MainWindow",
            "Patch archive: %1\nSource directory: %2\nSelect an existing game to which the patches will be added.")
            .arg(entryName, QDir::toNativeSeparators(source)), &dialog));

    QLineEdit *search = new QLineEdit(&dialog);
    search->setPlaceholderText(
        QCoreApplication::translate("MainWindow", "Filter by game name, platform, or directory"));
    layout->addWidget(search);
    QComboBox *choices = new QComboBox(&dialog);
    layout->addWidget(choices);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(
        QCoreApplication::translate("MainWindow", "Assign to This Game"));
    buttons->button(QDialogButtonBox::Cancel)->setText(
        QCoreApplication::translate("MainWindow", "Do Not Assign"));
    layout->addWidget(buttons);

    const auto refill = [games, choices, buttons, search]() {
        choices->clear();
        const QString keyword = search->text().trimmed();
        for (guint i = 0; i < games->len; i++) {
            const RhGame *game = static_cast<const RhGame *>(g_ptr_array_index(games, i));
            const QString name = QString::fromUtf8(game->name);
            const QString platform = QString::fromUtf8(game->platform);
            const QString dir = QString::fromUtf8(game->dir_path);
            if (!keyword.isEmpty() && !name.contains(keyword, Qt::CaseInsensitive)
                && !platform.contains(keyword, Qt::CaseInsensitive)
                && !dir.contains(keyword, Qt::CaseInsensitive))
                continue;
            choices->addItem(QStringLiteral("%1 [%2] — %3").arg(name, platform, dir),
                             QString::fromUtf8(game->id));
        }
        buttons->button(QDialogButtonBox::Ok)->setEnabled(choices->count() > 0);
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, refill);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refill();

    return dialog.exec() == QDialog::Accepted ? choices->currentData().toString()
                                              : QString();
}

void MainWindow::startImport(const QStringList &directories)
{
    if (m_readOnly) {
        QMessageBox::warning(this, tr("Read-Only Library"),
                             tr("The current library is open in read-only mode; importing is unavailable."));
        return;
    }
    if (m_importing) {
        QMessageBox::information(this, tr("Import"),
                                 tr("An import task is already in progress."));
        return;
    }
    if (m_rules == nullptr) {
        QMessageBox::warning(this, tr("Import"),
                             tr("Platform rules are not loaded; importing is unavailable."));
        return;
    }

    QStringList dirs;
    QHash<QString, QStringList> files;
    for (const QString &path : directories) {
        QFileInfo info(path);
        if (info.isDir()) {
            const QString dir = info.absoluteFilePath();
            if (QFileInfo(dir).isDir() && !dirs.contains(dir))
                dirs.append(dir);
        } else {
            const QString dir = info.absolutePath();
            if (!QFileInfo(dir).isDir())
                continue;
            if (!dirs.contains(dir))
                dirs.append(dir);
            files[dir].append(info.fileName());
        }
    }
    if (dirs.isEmpty())
        return;

    m_importing = true;

    ImportScanThread *scan = new ImportScanThread(m_rules, m_root, dirs, files, this);

    QProgressDialog *pulse = new QProgressDialog(
        tr("Prescanning source directories…"), QString(), 0, 0, this);
    pulse->setWindowTitle(tr("Import"));
    pulse->setWindowModality(Qt::WindowModal);
    pulse->setMinimumDuration(0);
    pulse->setAutoClose(false);
    pulse->setAutoReset(false);
    pulse->setCancelButton(nullptr);
    pulse->show();

    connect(scan, &ImportScanThread::scanned, pulse,
            [this, pulse](int done, int total, const QString &source) {
                pulse->setLabelText(
                    tr("Prescanning (%1/%2): %3")
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
        guint unassignedPatchArchives = 0;
        guint mixedArchives = 0;
        guint ambiguousArchives = 0;
        guint pendingArchives = 0;
        guint guessedNames = 0;
        guint escapedNames = 0;

        for (const ImportSourcePlan &result : results) {
            if (!result.error.isEmpty())
                errors.append(tr("%1: %2").arg(
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
        }

        GPtrArray *patchTargets = nullptr;
        if (patchArchives > 0) {
            RhGameFilter filter = {0};
            filter.order_by = "name";
            filter.limit = -1;
            RhErr targetErr = RH_OK;
            patchTargets = store_games_list(m_store, &filter, &targetErr);
            if (targetErr != RH_OK)
                errors.append(tr("Failed to read the game list for patch assignment: %1")
                                  .arg(QString::fromUtf8(rh_err_str(targetErr))));
            if (patchTargets != nullptr && patchTargets->len == 0)
                QMessageBox::information(
                    this, tr("Patch-Only Archive Assignment"),
                    tr("Patch-only archives can only be assigned to existing games; the current library is empty, so these archives will be skipped."));
        }

        /* 所有平台和归属询问都集中在主线程；执行线程保持零交互。 */
        const QByteArray rootUtf8 = m_root.toUtf8();
        for (const ImportSourcePlan &result : results) {
            if (result.plan == nullptr)
                continue;

            for (guint i = 0; i < result.plan->entries->len; i++) {
                RhImportPlanEntry *entry = static_cast<RhImportPlanEntry *>(
                    g_ptr_array_index(result.plan->entries, i));

                if (entry->is_patch_bundle) {
                    const QString gameId = choosePatchTarget(
                        this, patchTargets, QString::fromUtf8(entry->name), result.source);
                    if (!gameId.isEmpty()) {
                        const QByteArray idUtf8 = gameId.toUtf8();
                        rh_import_entry_assign_patch_target(entry, m_store,
                                                            idUtf8.constData());
                    }
                } else if (entry->platform == nullptr) {
                    GPtrArray *found = entry->roms->len > 0
                        ? rh_platform_candidates_for_file(
                              m_rules,
                              static_cast<const gchar *>(g_ptr_array_index(entry->roms, 0)))
                        : nullptr;
                    const GPtrArray *platforms = found != nullptr && found->len > 0
                        ? found : rh_platform_all(m_rules);

                    if (platforms->len > 0) {
                        QStringList choices{tr("Unspecified (skipped during execution)")};
                        for (guint p = 0; p < platforms->len; p++) {
                            const RhPlatform *platform = static_cast<const RhPlatform *>(
                                g_ptr_array_index(platforms, p));
                            choices.append(QString::fromUtf8(platform->id));
                        }

                        bool ok = false;
                        const QString picked = QInputDialog::getItem(
                            this, tr("Select Platform"),
                            tr("Entry: %1\nSource directory: %2\n"
                               "Select a platform; unspecified entries will be skipped during execution.")
                                .arg(QString::fromUtf8(entry->name),
                                     QDir::toNativeSeparators(result.source)),
                            choices, 0, false, &ok);
                        const int choice = choices.indexOf(picked);
                        if (ok && choice > 0) {
                            const RhPlatform *platform = static_cast<const RhPlatform *>(
                                g_ptr_array_index(platforms, choice - 1));
                            rh_import_entry_assign_platform(entry, platform->id,
                                                            rootUtf8.constData());
                        }
                    }
                    if (found != nullptr)
                        g_ptr_array_free(found, TRUE);
                }

                mediaCount += static_cast<int>(entry->media->len);
                patchCount += static_cast<int>(entry->patches->len);
                if (entry->is_patch_bundle) {
                    if (entry->target_game_id == nullptr)
                        unassignedPatchArchives++;
                } else if (entry->platform == nullptr) {
                    ambiguousEntries++;
                    if (entry->roms->len > 0
                        && rh_platform_is_archive(
                            m_rules,
                            static_cast<const gchar *>(g_ptr_array_index(entry->roms, 0))))
                        pendingArchives++;
                }
                if (entry->target_exists)
                    existingTargets++;
            }
        }
        if (patchTargets != nullptr)
            g_ptr_array_unref(patchTargets);

        /* 平台已确定后再查重；只有用户确认处置后才进入执行阶段。 */
        int duplicateCount = 0;
        for (RhImportPlan *plan : plans) {
            RhErr dupErr = RH_OK;
            rh_import_plan_detect_duplicates(plan, m_store, rootUtf8.constData(),
                                             m_rules, &dupErr);
            if (dupErr != RH_OK) {
                errors.append(tr("Failed to check duplicates: %1")
                                  .arg(QString::fromUtf8(rh_err_str(dupErr))));
            }
            duplicateCount += countImportDuplicates(plan);
        }
        if (duplicateCount > 0) {
            ImportDupsDialog duplicates(plans, this);
            if (duplicates.exec() != QDialog::Accepted) {
                rh_import_plans_free(plans);
                m_importing = false;
                return;
            }
            duplicates.applyDecisions();
        }

        /* 未指定平台或归属的条目执行时跳过，不计入确认数量。 */
        const int importable = entries - ambiguousEntries
                             - static_cast<int>(unassignedPatchArchives);
        const int assignedPatchArchives = static_cast<int>(patchArchives)
                                        - static_cast<int>(unassignedPatchArchives);
        const int newGames = importable - assignedPatchArchives;

        const QString detailText =
            tr("Files scanned: %1\n"
               "Entries to import: %2 (new games %15, patch-only archives %16)\n"
               "Media files: %3, patch files: %4\n"
               "Skipped: unknown types %5, top-level media %6\n"
               "Patch-only archives: %7 total, unassigned %17 (skipped during execution)\n"
               "Archives with undetermined platforms: mixed %8, ambiguous %9"
               " (including unspecified %10, skipped during execution)\n"
               "Undetermined platform (skipped during execution): %11\n"
               "Target directory already exists (skipped during execution): %12\n"
               "Encoding-guessed names: %13, escaped names: %14")
                .arg(filesScanned)
                .arg(importable)
                .arg(mediaCount)
                .arg(patchCount)
                .arg(skippedUnknown)
                .arg(skippedMedia)
                .arg(patchArchives)
                .arg(mixedArchives)
                .arg(ambiguousArchives)
                .arg(pendingArchives)
                .arg(ambiguousEntries)
                .arg(existingTargets)
                .arg(guessedNames)
                .arg(escapedNames)
                .arg(newGames)
                .arg(assignedPatchArchives)
                .arg(unassignedPatchArchives);

        if (importable == 0) {
            rh_import_plans_free(plans);
            m_importing = false;

            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle(tr("Import"));
            box.setText(entries == 0
                ? tr("No entries can be imported.")
                : tr("All %1 entries fail the import requirements and will be skipped during execution.")
                      .arg(entries));
            box.setDetailedText(detailText +
                                (errors.isEmpty()
                                     ? QString()
                                     : tr("\n\nErrors:\n") + errors.join('\n')));
            box.exec();
            return;
        }

        QMessageBox confirm(this);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(tr("Confirm Import"));
        confirm.setText(tr("%1 import entries will be processed (new games %2, patch-only archives %3). Continue?")
                            .arg(importable).arg(newGames).arg(assignedPatchArchives));
        QCheckBox *datNames = new QCheckBox(
            tr("Use DAT standard file names when matched (recommended)"), &confirm);
        datNames->setChecked(true);
        confirm.setCheckBox(datNames);
        confirm.setDetailedText(detailText);
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::Yes);
        if (!errors.isEmpty())
            confirm.setInformativeText(
                tr("%1 source directories failed to scan; see details.").arg(errors.size()));

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
            jobId, true, true,
            datNames->isChecked() ? RH_IMPORT_NAMES_DAT : RH_IMPORT_NAMES_ORIGINAL,
            this);

        QProgressDialog *progress = new QProgressDialog(
            tr("Importing…"), tr("Cancel"), 0, entries, this);
        progress->setWindowTitle(tr("Import"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
        progress->show();

        connect(exec, &ImportExecThread::progressChanged, progress,
                [this, progress](int done, int total, const QString &label) {
                    progress->setMaximum(total > 0 ? total : 1);
                    progress->setValue(done);
                    if (!label.isEmpty())
                        progress->setLabelText(tr("Importing: %1").arg(label));
                });
        connect(progress, &QProgressDialog::canceled, exec, [this, exec, progress]() {
            exec->cancel();
            progress->setLabelText(tr("Canceling…"));
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
                finishError = tr("User canceled");

            if (jobId > 0) {
                const QByteArray utf8 = finishError.toUtf8();
                store_job_finish(m_store, jobId,
                                 finishError.isEmpty() ? nullptr : utf8.constData());
            }

            m_importing = false;
            m_model->reload();
            refreshPlatforms();
            updateStatus();

            QString text = tr("Import finished: %1 succeeded, %2 skipped, %3 failed.")
                               .arg(stats.imported).arg(stats.skipped).arg(stats.failed);
            if (stats.cancelled)
                text = tr("Import canceled.") + text;
            else if (failed)
                text += tr("\nError: %1").arg(error);
            QMessageBox::information(this, tr("Import"), text);
        });

        exec->start();
    });

    scan->start();
}
