#include "dat-dialog.h"

#include <sys/stat.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <glib/gstdio.h>

DatFetchThread::DatFetchThread(GPtrArray *sources, const QString &dir,
                               const QString &dbPath, bool checkOnly,
                               bool importAfter, QObject *parent)
    : QThread(parent), m_sources(sources), m_dir(dir), m_dbPath(dbPath),
      m_checkOnly(checkOnly), m_importAfter(importAfter)
{
}

void DatFetchThread::requestCancel()
{
    m_cancel.store(true);
}

bool DatFetchThread::isCancelled() const
{
    return m_cancel.load();
}

namespace {

struct FetchCtx {
    DatFetchThread *thread;
    int row;
    int lastPercent;
};

gboolean fetch_progress(const gchar *url, guint64 done, guint64 total,
                        gpointer data)
{
    (void)url;
    FetchCtx *ctx = static_cast<FetchCtx *>(data);
    if (ctx->thread->isCancelled())
        return FALSE;
    if (total == 0)
        return TRUE;
    int percent = static_cast<int>(done * 100 / total);
    if (percent != ctx->lastPercent) {
        ctx->lastPercent = percent;
        ctx->thread->rowProgress(ctx->row, percent);
    }
    return TRUE;
}

QString formatBytes(guint64 bytes)
{
    return QLocale::system().formattedDataSize(static_cast<qint64>(bytes));
}

} // namespace

void DatFetchThread::run()
{
    RhStore *store = nullptr;
    if (m_importAfter) {
        RhErr err = RH_OK;
        store = store_open(m_dbPath.toUtf8().constData(), &err);
        if (store == nullptr) {
            emit failedToStart(tr("Cannot open the library: %1")
                                   .arg(QString::fromUtf8(rh_err_str(err))));
            emit finishedSummary(0, 0, 0, false, m_checkOnly);
            return;
        }
    }

    int updated = 0;
    int unchanged = 0;
    int available = 0;
    int failed = 0;
    bool cancelled = false;

    for (guint i = 0; i < m_sources->len; i++) {
        if (m_cancel.load()) {
            cancelled = true;
            break;
        }

        const RhDatSource *source =
            static_cast<const RhDatSource *>(g_ptr_array_index(m_sources, i));
        const int row = static_cast<int>(i);

        RhDatFetchOptions options = { 0 };
        options.check_only = m_checkOnly;
        FetchCtx ctx = { this, row, -1 };
        options.progress = fetch_progress;
        options.user_data = &ctx;

        RhDatFetchResult result;
        RhErr err = rh_dat_fetch(source, m_dir.toUtf8().constData(), &options,
                                 &result);

        if (err == RH_ERR_CANCELLED) {
            rh_dat_fetch_result_clear(&result);
            cancelled = true;
            break;
        }
        if (err != RH_OK) {
            failed++;
            QString detail = result.message != nullptr
                ? QString::fromUtf8(result.message)
                : QString::fromUtf8(rh_err_str(err));
            emit rowStatus(row, tr("Failed: %1").arg(detail));
            rh_dat_fetch_result_clear(&result);
            emit rowDone(row);
            continue;
        }

        if (m_checkOnly) {
            if (result.changed) {
                available++;
                emit rowStatus(row, tr("Update available (%1)")
                                        .arg(formatBytes(result.bytes)));
            } else {
                unchanged++;
                emit rowStatus(row, tr("Up to date"));
            }
        } else if (result.installed) {
            updated++;
            emit rowStatus(row, tr("Updated (%1)").arg(formatBytes(result.bytes)));
        } else {
            unchanged++;
            emit rowStatus(row, tr("Up to date"));
        }

        if (!m_checkOnly && m_importAfter && store != nullptr) {
            RhDatImportOptions importOptions = { 0 };
            importOptions.platform = source->platform;
            importOptions.dat_name = source->name;
            RhDatImportStats stats;
            rh_dat_import_stats_clear(&stats);
            RhErr importErr = rh_dat_import(store, result.path, &importOptions,
                                            &stats);
            if (importErr != RH_OK) {
                failed++;
                emit rowStatus(row, tr("Index failed: %1")
                                        .arg(QString::fromUtf8(
                                            rh_err_str(importErr))));
            } else if (!stats.unchanged) {
                emit rowStatus(row, tr("Indexed %1 entries")
                                        .arg(QString::number(stats.entry_count)));
            }
        }

        rh_dat_fetch_result_clear(&result);
        emit rowDone(row);
    }

    if (store != nullptr)
        store_close(store);
    emit finishedSummary(m_checkOnly ? unchanged : updated, available, failed,
                         cancelled, m_checkOnly);
}

DatDialog::DatDialog(const QString &dbPath, QWidget *parent)
    : QDialog(parent), m_dbPath(dbPath)
{
    setWindowTitle(tr("DAT Sources"));
    resize(820, 520);

    m_manifest = rh_dat_sources_default_path();
    gchar *dir = rh_path_dat_cache_dir();
    m_dir = QString::fromUtf8(dir);
    g_free(dir);

    m_manifestLabel = new QLabel(this);
    m_manifestLabel->setWordWrap(true);
    m_manifestLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        { tr("Platform"), tr("Name"), tr("Local file"), tr("Result") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);

    m_importCheck = new QCheckBox(tr("Import into the library after download"), this);
    m_importCheck->setChecked(true);

    m_checkButton = new QPushButton(tr("Check for Updates"), this);
    m_updateButton = new QPushButton(tr("Download and Update"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_closeButton = new QPushButton(tr("Close"), this);
    m_cancelButton->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_importCheck);
    buttons->addStretch();
    buttons->addWidget(m_checkButton);
    buttons->addWidget(m_updateButton);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_manifestLabel);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(m_checkButton, &QPushButton::clicked, this, &DatDialog::checkAll);
    connect(m_updateButton, &QPushButton::clicked, this, &DatDialog::updateAll);
    connect(m_cancelButton, &QPushButton::clicked, this, &DatDialog::cancel);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);

    populate();
}

DatDialog::~DatDialog()
{
    stopThread();
    if (m_sources != nullptr)
        g_ptr_array_unref(m_sources);
    g_free(m_manifest);
}

void DatDialog::populate()
{
    if (m_manifest == nullptr) {
        m_manifestLabel->setText(tr("DAT source manifest not found. "
                                    "Create ~/.config/rom-hamster/dats/sources.conf "
                                    "or set ROM_HAMSTER_DAT_SOURCES."));
        m_checkButton->setEnabled(false);
        m_updateButton->setEnabled(false);
        return;
    }

    m_manifestLabel->setText(tr("Manifest: %1").arg(QString::fromUtf8(m_manifest)));

    gchar *message = nullptr;
    RhErr err = rh_dat_sources_load(m_manifest, &m_sources, &message);
    if (err != RH_OK) {
        m_manifestLabel->setText(tr("Cannot load DAT sources: %1")
                                     .arg(message != nullptr
                                              ? QString::fromUtf8(message)
                                              : QString::fromUtf8(rh_err_str(err))));
        g_free(message);
        m_checkButton->setEnabled(false);
        m_updateButton->setEnabled(false);
        return;
    }
    g_free(message);

    m_table->setRowCount(static_cast<int>(m_sources->len));
    for (guint i = 0; i < m_sources->len; i++) {
        const RhDatSource *source =
            static_cast<const RhDatSource *>(g_ptr_array_index(m_sources, i));
        const int row = static_cast<int>(i);
        const QString platform = QString::fromUtf8(source->platform);
        const QString name = source->name != nullptr
            ? QString::fromUtf8(source->name) : QString();

        gchar *path = g_build_filename(m_dir.toUtf8().constData(), source->file, NULL);
        QString local = tr("Not downloaded");
        GStatBuf st;
        if (g_stat(path, &st) == 0 && (st.st_mode & S_IFREG) != 0)
            local = formatBytes(static_cast<guint64>(st.st_size));
        g_free(path);

        m_table->setItem(row, 0, new QTableWidgetItem(platform));
        m_table->setItem(row, 1, new QTableWidgetItem(name));
        m_table->setItem(row, 2, new QTableWidgetItem(local));
        m_table->setItem(row, 3, new QTableWidgetItem(QString()));
    }
}

void DatDialog::checkAll()
{
    start(true, false);
}

void DatDialog::updateAll()
{
    start(false, m_importCheck->isChecked());
}

void DatDialog::start(bool checkOnly, bool importAfter)
{
    if (m_sources == nullptr || m_sources->len == 0 || m_thread != nullptr)
        return;
    if (!rh_dat_fetch_supported()) {
        QMessageBox::warning(this, tr("DAT Sources"),
                             tr("This build has no DAT download support."));
        return;
    }

    for (int row = 0; row < m_table->rowCount(); row++)
        m_table->item(row, 3)->setText(QString());

    m_progress->setRange(0, static_cast<int>(m_sources->len));
    m_progress->setValue(0);
    m_status->setText(checkOnly ? tr("Checking…") : tr("Downloading…"));
    setRunning(true);

    m_thread = new DatFetchThread(m_sources, m_dir, m_dbPath, checkOnly,
                                  importAfter, this);
    connect(m_thread, &DatFetchThread::rowStatus, this, &DatDialog::rowStatus);
    connect(m_thread, &DatFetchThread::rowProgress, this, &DatDialog::rowProgress);
    connect(m_thread, &DatFetchThread::rowDone, this, &DatDialog::rowDone);
    connect(m_thread, &DatFetchThread::finishedSummary, this,
            &DatDialog::finishedSummary);
    connect(m_thread, &DatFetchThread::failedToStart, this,
            &DatDialog::failedToStart);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void DatDialog::cancel()
{
    if (m_thread != nullptr)
        m_thread->requestCancel();
}

void DatDialog::rowStatus(int row, const QString &text)
{
    if (row >= 0 && row < m_table->rowCount() && m_table->item(row, 3) != nullptr)
        m_table->item(row, 3)->setText(text);
}

void DatDialog::rowProgress(int row, int percent)
{
    if (row >= 0 && row < m_table->rowCount())
        m_status->setText(tr("%1: %2%")
                              .arg(m_table->item(row, 0)->text())
                              .arg(percent));
}

void DatDialog::rowDone(int row)
{
    (void)row;
    m_progress->setValue(m_progress->value() + 1);
}

void DatDialog::finishedSummary(int updated, int available, int failed,
                                bool cancelled, bool checkOnly)
{
    m_thread = nullptr;
    setRunning(false);
    m_progress->setValue(m_progress->maximum());

    QString text;
    if (checkOnly) {
        text = tr("Check finished: %1 update(s), %2 up to date, %3 failed")
                   .arg(available)
                   .arg(updated)
                   .arg(failed);
    } else {
        text = tr("Update finished: %1 updated, %2 failed")
                   .arg(updated)
                   .arg(failed);
    }
    if (cancelled)
        text += tr(" (cancelled)");
    m_status->setText(text);
}

void DatDialog::failedToStart(const QString &message)
{
    QMessageBox::warning(this, tr("DAT Sources"), message);
}

void DatDialog::setRunning(bool running)
{
    m_checkButton->setEnabled(!running);
    m_updateButton->setEnabled(!running);
    m_importCheck->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    m_closeButton->setEnabled(!running);
}

void DatDialog::stopThread()
{
    if (m_thread == nullptr)
        return;
    m_thread->requestCancel();
    m_thread->wait();
    m_thread = nullptr;
}

void DatDialog::closeEvent(QCloseEvent *event)
{
    if (m_thread != nullptr) {
        m_thread->requestCancel();
        m_thread->wait();
        m_thread = nullptr;
    }
    event->accept();
}
