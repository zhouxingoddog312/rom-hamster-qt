#include "import-job.h"

#include <cstring>

void rh_import_plans_free(QVector<RhImportPlan *> &plans)
{
    for (RhImportPlan *plan : plans) {
        if (plan != nullptr)
            rh_import_plan_free(plan);
    }
    plans.clear();
}

ImportScanThread::ImportScanThread(const RhPlatformRules *rules, const QString &root,
                                   const QStringList &sources, QObject *parent)
    : QThread(parent), m_rules(rules), m_root(root), m_sources(sources)
{
}

void ImportScanThread::run()
{
    const QByteArray root = m_root.toUtf8();
    const int total = m_sources.size();

    for (int i = 0; i < total; ++i) {
        const QString sourceText = m_sources.at(i);
        ImportSourcePlan item;
        item.source = sourceText;

        const QByteArray source = sourceText.toUtf8();
        RhErr err = RH_OK;
        item.plan = rh_import_plan_scan(m_rules, source.constData(), &err);
        if (item.plan != nullptr)
            rh_import_plan_set_root(item.plan, root.constData());
        if (err != RH_OK)
            item.error = QString::fromUtf8(rh_err_str(err));

        results.append(item);
        emit scanned(i + 1, total, sourceText);
    }
}

ImportExecThread::ImportExecThread(const RhPlatformRules *rules, const QString &dbPath,
                                   const QString &root, const QStringList &sources,
                                   const QVector<RhImportPlan *> &plans, gint64 jobId,
                                   bool matchDat, bool generateM3u, QObject *parent)
    : QThread(parent),
      m_rules(rules),
      m_dbPath(dbPath),
      m_root(root),
      m_sources(sources),
      m_plans(plans),
      m_jobId(jobId),
      m_matchDat(matchDat),
      m_generateM3u(generateM3u)
{
    for (const RhImportPlan *plan : m_plans) {
        if (plan != nullptr)
            m_total += static_cast<int>(plan->entries->len);
    }
}

ImportExecThread::~ImportExecThread()
{
    rh_import_plans_free(m_plans);
}

gboolean ImportExecThread::cancelCallback(gpointer data)
{
    ImportExecThread *self = static_cast<ImportExecThread *>(data);
    return self->m_cancel.load() ? TRUE : FALSE;
}

void ImportExecThread::progressCallback(guint done, guint, const gchar *name,
                                        gpointer data)
{
    ImportExecThread *self = static_cast<ImportExecThread *>(data);
    const int overall = self->m_base.load() + static_cast<int>(done);
    emit self->progressChanged(overall, self->m_total,
                               name != nullptr ? QString::fromUtf8(name) : QString());
}

void ImportExecThread::run()
{
    RhErr err = RH_OK;
    m_store = store_open(m_dbPath.toUtf8().constData(), &err);
    if (m_store == nullptr) {
        failed = true;
        error = QStringLiteral("无法打开数据库：%1")
                    .arg(QString::fromUtf8(rh_err_str(err)));
        return;
    }

    const QByteArray root = m_root.toUtf8();

    for (int i = 0; i < m_plans.size(); ++i) {
        if (m_cancel.load())
            break;

        RhImportPlan *plan = m_plans.at(i);
        if (plan == nullptr)
            continue;

        QString source = i < m_sources.size() ? m_sources.at(i) : QString();
        const QByteArray sourceUtf8 = source.toUtf8();
        const gchar *sourceDir = sourceUtf8.constData();

        RhImportExecOptions options;
        std::memset(&options, 0, sizeof(options));
        options.root = root.constData();
        options.source_dir = sourceDir;
        options.match_dat = m_matchDat ? TRUE : FALSE;
        options.generate_m3u = m_generateM3u ? TRUE : FALSE;
        options.job_id = m_jobId;
        options.cancel = cancelCallback;
        options.cancel_data = this;
        options.progress = progressCallback;
        options.user_data = this;

        RhImportExecStats part;
        std::memset(&part, 0, sizeof(part));
        RhErr exec_err = rh_import_execute(m_store, m_rules, plan, &options, &part);

        stats.imported += part.imported;
        stats.skipped += part.skipped;
        stats.failed += part.failed;
        stats.cancelled = stats.cancelled || part.cancelled;

        m_base.fetch_add(static_cast<int>(plan->entries->len));

        if (exec_err != RH_OK && exec_err != RH_ERR_CANCELLED) {
            failed = true;
            error = QString::fromUtf8(rh_err_str(exec_err));
            break;
        }
        if (part.cancelled)
            break;
    }

    /* 作业终态由主线程调用 store_job_finish 统一写入（与主连接同一库）。 */

    store_close(m_store);
    m_store = nullptr;
}
