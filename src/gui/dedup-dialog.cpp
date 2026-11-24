#include "dedup-dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

static QString formatBytes(qint64 value)
{
    return QString::number(value);
}

DedupThread::DedupThread(const QString &dbPath, const QString &root,
                         const QString &platform, bool includeMedia,
                         bool backfill, qint64 minSize, QObject *parent)
    : QThread(parent),
      m_dbPath(dbPath),
      m_root(root),
      m_platform(platform),
      m_includeMedia(includeMedia),
      m_backfill(backfill),
      m_minSize(minSize)
{
}

gboolean DedupThread::progressCallback(const char *path, guint done,
                                       guint total, gpointer user_data)
{
    auto *self = static_cast<DedupThread *>(user_data);
    emit self->progressChanged(static_cast<int>(done), static_cast<int>(total),
                               QString::fromUtf8(path != nullptr ? path : ""));
    return self->m_cancel ? FALSE : TRUE;
}

void DedupThread::run()
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
    const QByteArray platform = m_platform.toUtf8();
    RhDedupOptions options = {0};
    options.root = root.constData();
    options.platform = platform.isEmpty() ? nullptr : platform.constData();
    options.include_media = m_includeMedia;
    options.backfill = m_backfill;
    options.min_size = m_minSize;
    options.progress = m_backfill ? progressCallback : nullptr;
    options.user_data = this;

    GPtrArray *result = nullptr;
    err = rh_dedup_report(store, &options, &result, &stats);
    store_close(store);

    if (err != RH_OK) {
        failed = true;
        error = QString::fromUtf8(rh_err_str(err));
        if (result != nullptr)
            g_ptr_array_unref(result);
        return;
    }
    groups = result;
}

DedupDialog::DedupDialog(const QString &dbPath, const QString &root,
                         QWidget *parent)
    : QDialog(parent), m_dbPath(dbPath), m_root(root)
{
    setWindowTitle(tr("Find Duplicates"));
    resize(980, 620);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Platform"), this));
    m_platform = new QComboBox(this);
    m_platform->setMinimumWidth(140);
    top->addWidget(m_platform);

    m_media = new QCheckBox(tr("Include media and patches"), this);
    m_media->setChecked(true);
    top->addWidget(m_media);

    m_backfill = new QCheckBox(tr("Compute missing hashes"), this);
    top->addWidget(m_backfill);

    top->addWidget(new QLabel(tr("Min size"), this));
    m_minSize = new QComboBox(this);
    m_minSize->setEditable(true);
    m_minSize->addItem(QStringLiteral("0"), QVariant::fromValue<qint64>(0));
    m_minSize->addItem(QStringLiteral("1048576"), QVariant::fromValue<qint64>(1 << 20));
    m_minSize->addItem(QStringLiteral("10485760"), QVariant::fromValue<qint64>(10 << 20));
    top->addWidget(m_minSize);

    m_scan = new QPushButton(tr("Scan"), this);
    top->addWidget(m_scan);
    top->addStretch(1);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {tr("Type"), tr("Hash"), tr("Size"), tr("Copies"), tr("Wasted"),
         tr("Paths")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_summary = new QLabel(this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    connect(m_scan, &QPushButton::clicked, this, &DedupDialog::reload);

    showPlatforms();
    reload();
}

DedupDialog::~DedupDialog()
{
    if (m_thread != nullptr && m_thread->isRunning()) {
        m_thread->cancel();
        m_thread->wait();
    }
}

void DedupDialog::showPlatforms()
{
    m_platform->addItem(tr("All platforms"), QString());
    RhErr err = RH_OK;
    const QByteArray db = m_dbPath.toUtf8();
    RhStore *store = store_open(db.constData(), &err);
    if (store == nullptr)
        return;
    GPtrArray *stats = store_stats_by_platform(store, &err);
    if (stats != nullptr) {
        for (guint i = 0; i < stats->len; i++) {
            RhPlatformStats *item = static_cast<RhPlatformStats *>(
                g_ptr_array_index(stats, i));
            if (item->platform != nullptr)
                m_platform->addItem(QString::fromUtf8(item->platform),
                                    QString::fromUtf8(item->platform));
        }
        g_ptr_array_unref(stats);
    }
    store_close(store);
}

void DedupDialog::reload()
{
    if (m_running)
        return;

    bool ok = false;
    const qint64 minSize = m_minSize->currentText().toLongLong(&ok);
    if (!ok || minSize < 0) {
        m_summary->setText(tr("Min size must be a non-negative number."));
        return;
    }

    m_running = true;
    m_scan->setEnabled(false);
    m_summary->setText(m_backfill->isChecked() ? tr("Computing hashes…")
                                               : tr("Scanning…"));

    auto *thread = new DedupThread(
        m_dbPath, m_root, m_platform->currentData().toString(),
        m_media->isChecked(), m_backfill->isChecked(), minSize, this);
    m_thread = thread;

    connect(thread, &DedupThread::progressChanged, this,
            [this](int done, int total, const QString &path) {
                m_summary->setText(tr("Hashing %1/%2: %3")
                                       .arg(done).arg(total).arg(path));
            });
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread)
            m_thread = nullptr;
        m_running = false;
        m_scan->setEnabled(true);
        if (thread->failed) {
            m_summary->setText(tr("Failed: %1").arg(thread->error));
        } else {
            showGroups(thread->groups, thread->stats);
            thread->groups = nullptr;
        }
        thread->deleteLater();
    });

    thread->start();
}

void DedupDialog::showGroups(GPtrArray *groups, const RhDedupStats &stats)
{
    m_table->setRowCount(0);

    if (groups == nullptr) {
        m_summary->setText(tr("No duplicates found."));
        return;
    }

    for (guint i = 0; i < groups->len; i++) {
        RhDupGroup *group = static_cast<RhDupGroup *>(
            g_ptr_array_index(groups, i));
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        QStringList paths;
        for (guint j = 0; j < group->members->len; j++) {
            RhDupMember *member = static_cast<RhDupMember *>(
                g_ptr_array_index(group->members, j));
            QString path = QStringLiteral("%1/%2")
                               .arg(QString::fromUtf8(member->dir_path),
                                    QString::fromUtf8(member->filename));
            if (member->kind != nullptr)
                path = QStringLiteral("[%1] %2").arg(
                    QString::fromUtf8(member->kind), path);
            if (member->missing)
                path += QStringLiteral(" (missing)");
            paths.append(path);
        }

        const qint64 wasted = group->size
                              * static_cast<qint64>(group->members->len - 1);
        m_table->setItem(row, 0, new QTableWidgetItem(
            QString::fromUtf8(group->kind)));
        m_table->setItem(row, 1, new QTableWidgetItem(
            QString::fromUtf8(group->key).left(12)));
        m_table->setItem(row, 2, new QTableWidgetItem(formatBytes(group->size)));
        m_table->setItem(row, 3, new QTableWidgetItem(
            QString::number(group->members->len)));
        m_table->setItem(row, 4, new QTableWidgetItem(formatBytes(wasted)));
        m_table->setItem(row, 5, new QTableWidgetItem(paths.join('\n')));
    }
    m_table->resizeRowsToContents();

    QString summary = tr("%1 duplicate clusters, %2 extra copies, %3 bytes wasted.")
                          .arg(stats.clusters)
                          .arg(stats.extra_copies)
                          .arg(stats.bytes_duplicated);
    if (stats.hashed > 0 || stats.failed > 0) {
        summary += QStringLiteral(" ");
        summary += tr("Hashed %1, failed/missing %2.")
                       .arg(stats.hashed).arg(stats.failed);
    }
    m_summary->setText(summary);

    g_ptr_array_unref(groups);
}
