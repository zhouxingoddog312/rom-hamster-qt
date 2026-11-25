#include "export-dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

/* 规格文件候选：安装/开发数据目录下的 frontends/，外加用户覆盖目录。 */
static QStringList frontendSpecFiles()
{
    QStringList dirs;
    GPtrArray *candidates = rh_path_data_dir_candidates();
    for (guint i = 0; i < candidates->len; i++) {
        const char *dir = static_cast<const char *>(g_ptr_array_index(candidates, i));
        dirs.append(QDir(QString::fromUtf8(dir)).filePath(QStringLiteral("frontends")));
    }
    g_ptr_array_free(candidates, TRUE);
    dirs.append(QDir(QString::fromUtf8(g_get_user_config_dir()))
                    .filePath(QStringLiteral("rom-hamster/frontends")));

    QStringList files;
    for (const QString &dir : dirs) {
        const QFileInfoList entries =
            QDir(dir).entryInfoList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString path = entry.absoluteFilePath();
            if (!files.contains(path))
                files.append(path);
        }
    }
    return files;
}

ExportThread::ExportThread(const QString &dbPath, const QString &root,
                           const RhPlatformRules *rules, const QString &specPath,
                           const QString &outputDir, bool copyRoms, bool copyMedia,
                           bool generateM3u, RhExportNameMode nameMode, QObject *parent)
    : QThread(parent),
      m_dbPath(dbPath),
      m_root(root),
      m_rules(rules),
      m_specPath(specPath),
      m_outputDir(outputDir),
      m_copyRoms(copyRoms),
      m_copyMedia(copyMedia),
      m_generateM3u(generateM3u),
      m_nameMode(nameMode)
{
}

void ExportThread::progressCallback(guint done, guint total, const char *name,
                                    gpointer user_data)
{
    auto *self = static_cast<ExportThread *>(user_data);
    emit self->progressChanged(static_cast<int>(done), static_cast<int>(total),
                               QString::fromUtf8(name != nullptr ? name : ""));
}

void ExportThread::run()
{
    const QByteArray db = m_dbPath.toUtf8();
    RhErr err = RH_OK;
    RhStore *store = store_open(db.constData(), &err);
    if (store == nullptr) {
        failed = true;
        error = QString::fromUtf8(rh_err_str(err));
        return;
    }

    const QByteArray root = m_root.toUtf8();
    const QByteArray spec = m_specPath.toUtf8();
    const QByteArray output = m_outputDir.toUtf8();
    RhExportOptions options = {0};
    options.library_root = root.constData();
    options.output_dir = output.constData();
    options.copy_roms = m_copyRoms;
    options.copy_media = m_copyMedia;
    options.compress_roms = FALSE;
    options.generate_m3u = m_generateM3u;
    options.name_mode = m_nameMode;

    err = rh_export_run(store, m_rules, spec.constData(), &options,
                        progressCallback, this, &stats);
    store_close(store);

    if (err != RH_OK) {
        failed = true;
        error = QString::fromUtf8(rh_err_str(err));
    }
}

ExportDialog::ExportDialog(const QString &dbPath, const QString &root,
                           const RhPlatformRules *rules, QWidget *parent)
    : QDialog(parent), m_dbPath(dbPath), m_root(root), m_rules(rules)
{
    setWindowTitle(tr("Export Front-end Manifest"));
    resize(680, 0);

    auto *intro = new QLabel(
        tr("Generate a front-end manifest from the current library. "
           "The library itself is not modified."),
        this);
    intro->setWordWrap(true);

    auto *form = new QFormLayout();

    auto *specRow = new QHBoxLayout();
    m_spec = new QComboBox(this);
    m_spec->setMinimumWidth(360);
    const QStringList specs = frontendSpecFiles();
    for (const QString &path : specs) {
        m_spec->addItem(QFileInfo(path).completeBaseName(), path);
        m_spec->setItemData(m_spec->count() - 1, path, Qt::ToolTipRole);
    }
    specRow->addWidget(m_spec, 1);
    auto *browseSpec = new QPushButton(tr("Browse…"), this);
    specRow->addWidget(browseSpec);
    form->addRow(tr("Front-end spec:"), specRow);

    auto *outRow = new QHBoxLayout();
    m_output = new QLineEdit(this);
    m_output->setPlaceholderText(tr("Choose an output directory…"));
    outRow->addWidget(m_output, 1);
    auto *browseOutput = new QPushButton(tr("Browse…"), this);
    outRow->addWidget(browseOutput);
    form->addRow(tr("Output directory:"), outRow);

    m_copyRoms = new QCheckBox(tr("Copy ROM files into the output"), this);
    m_copyRoms->setChecked(true);
    m_copyMedia = new QCheckBox(tr("Copy media files into the output"), this);
    m_copyMedia->setChecked(true);
    m_m3u = new QCheckBox(tr("Generate .m3u for multi-disc games"), this);

    m_names = new QComboBox(this);
    m_names->addItem(tr("Original (library file name)"), int(RH_EXPORT_NAMES_ORIGINAL));
    m_names->addItem(tr("DAT standard name"), int(RH_EXPORT_NAMES_DAT));
    m_names->addItem(tr("Game display name"), int(RH_EXPORT_NAMES_DISPLAY));

    form->addRow(m_copyRoms);
    form->addRow(m_copyMedia);
    form->addRow(m_m3u);
    form->addRow(tr("ROM file names:"), m_names);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_export = buttons->addButton(tr("Export"), QDialogButtonBox::ActionRole);
    m_export->setDefault(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(browseSpec, &QPushButton::clicked, this, &ExportDialog::chooseSpec);
    connect(browseOutput, &QPushButton::clicked, this, &ExportDialog::chooseOutput);
    connect(m_export, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (specs.isEmpty())
        m_export->setEnabled(false);
}

ExportDialog::~ExportDialog()
{
    if (m_thread != nullptr && m_thread->isRunning())
        m_thread->wait();
}

void ExportDialog::chooseSpec()
{
    const QString start = m_spec->count() > 0
        ? QFileInfo(m_spec->currentData().toString()).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Front-end Specification"), start,
        tr("Front-end specs (*.conf);;All files (*)"));
    if (path.isEmpty())
        return;

    int index = m_spec->findData(path);
    if (index < 0) {
        m_spec->addItem(QFileInfo(path).completeBaseName(), path);
        index = m_spec->count() - 1;
    }
    m_spec->setCurrentIndex(index);
    m_export->setEnabled(true);
}

void ExportDialog::chooseOutput()
{
    const QString start = m_output->text().isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : m_output->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Export Output Directory"), start);
    if (!dir.isEmpty())
        m_output->setText(QDir::toNativeSeparators(dir));
}

void ExportDialog::startExport()
{
    if (m_running)
        return;

    const QString specPath = m_spec->currentData().toString();
    if (specPath.isEmpty() || !QFileInfo::exists(specPath)) {
        QMessageBox::warning(this, tr("Export"),
                             tr("Select a front-end specification first."));
        return;
    }

    QString outputDir = QDir::fromNativeSeparators(m_output->text().trimmed());
    if (outputDir.isEmpty()) {
        QMessageBox::warning(this, tr("Export"),
                             tr("Select an output directory first."));
        return;
    }

    /* 先在主线程加载规格，语法错误能带「文件:行号」提示给用户。 */
    RhErr err = RH_OK;
    gchar *message = nullptr;
    const QByteArray specUtf8 = specPath.toUtf8();
    RhExportSpec *spec = rh_export_spec_load(specUtf8.constData(), &err, &message);
    if (spec == nullptr) {
        QMessageBox::warning(
            this, tr("Export"),
            tr("Cannot load the front-end specification: %1")
                .arg(message != nullptr ? QString::fromUtf8(message)
                                        : QString::fromUtf8(rh_err_str(err))));
        g_free(message);
        return;
    }
    rh_export_spec_free(spec);
    g_free(message);

    if (!QDir().mkpath(outputDir)) {
        QMessageBox::warning(this, tr("Export"),
                             tr("Cannot create the output directory: %1")
                                 .arg(QDir::toNativeSeparators(outputDir)));
        return;
    }

    m_running = true;
    m_export->setEnabled(false);
    m_spec->setEnabled(false);
    m_output->setEnabled(false);
    m_copyRoms->setEnabled(false);
    m_copyMedia->setEnabled(false);
    m_m3u->setEnabled(false);
    m_names->setEnabled(false);

    m_progress = new QProgressDialog(tr("Exporting…"), QString(), 0, 0, this);
    m_progress->setWindowTitle(tr("Export"));
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    m_progress->setCancelButton(nullptr);
    m_progress->show();

    m_thread = new ExportThread(
        m_dbPath, m_root, m_rules, specPath, QDir::fromNativeSeparators(outputDir),
        m_copyRoms->isChecked(), m_copyMedia->isChecked(), m_m3u->isChecked(),
        static_cast<RhExportNameMode>(m_names->currentData().toInt()), this);

    connect(m_thread, &ExportThread::progressChanged, this,
            [this](int done, int total, const QString &name) {
                if (m_progress == nullptr)
                    return;
                m_progress->setMaximum(total > 0 ? total : 1);
                m_progress->setValue(done);
                if (!name.isEmpty())
                    m_progress->setLabelText(tr("Exporting: %1").arg(name));
            });
    connect(m_thread, &QThread::finished, this, &ExportDialog::finishExport);
    m_thread->start();
}

void ExportDialog::finishExport()
{
    if (m_progress != nullptr) {
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }

    const RhExportStats stats = m_thread != nullptr ? m_thread->stats : RhExportStats{};
    const bool failed = m_thread != nullptr && m_thread->failed;
    const QString error = m_thread != nullptr ? m_thread->error : QString();
    if (m_thread != nullptr) {
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    m_running = false;
    m_export->setEnabled(m_spec->count() > 0);
    m_spec->setEnabled(true);
    m_output->setEnabled(true);
    m_copyRoms->setEnabled(true);
    m_copyMedia->setEnabled(true);
    m_m3u->setEnabled(true);
    m_names->setEnabled(true);

    if (failed) {
        QMessageBox::warning(this, tr("Export"),
                             tr("Export failed: %1").arg(error));
        return;
    }

    const QString summary =
        tr("Export finished: %1 games, %2 ROM files, %3 media files, %4 errors.")
            .arg(stats.games).arg(stats.roms).arg(stats.media).arg(stats.errors);
    if (stats.errors > 0)
        QMessageBox::warning(this, tr("Export"), summary);
    else
        QMessageBox::information(this, tr("Export"), summary);
}
