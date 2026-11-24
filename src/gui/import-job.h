/*
 * import-job.h —— 两阶段导入的后台线程（ARCHITECTURE.md §6.2）
 *
 * ① ImportScanThread：预扫描，只读，产出计划；
 * ② ImportExecThread：执行，零交互，写库与复制文件。
 * 两者都不碰 QWidget：进度经信号（AutoConnection 自动排队）回到主线程。
 */
#ifndef RH_GUI_IMPORT_JOB_H
#define RH_GUI_IMPORT_JOB_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>

#include "core-bridge.h"

struct ImportSourcePlan {
    QString       source;
    RhImportPlan *plan = nullptr;
    QString       error;
};

void rh_import_plans_free(QVector<RhImportPlan *> &plans);

class ImportScanThread : public QThread {
    Q_OBJECT
public:
    ImportScanThread(const RhPlatformRules *rules, const QString &root,
                     const QStringList &sources,
                     const QHash<QString, QStringList> &files = {},
                     QObject *parent = nullptr);

    /* run() 结束后有效；plan 的所有权交给调用方 */
    QVector<ImportSourcePlan> results;

signals:
    void scanned(int done, int total, const QString &source);

protected:
    void run() override;

private:
    const RhPlatformRules *m_rules;
    QString m_root;
    QStringList m_sources;
    QHash<QString, QStringList> m_files;  /* 非空时只扫描列出的文件 */
};

class ImportExecThread : public QThread {
    Q_OBJECT
public:
    ImportExecThread(const RhPlatformRules *rules, const QString &dbPath,
                     const QString &root, const QStringList &sources,
                     const QVector<RhImportPlan *> &plans, gint64 jobId,
                     bool matchDat, bool generateM3u,
                     RhImportNameMode nameMode, QObject *parent = nullptr);
    ~ImportExecThread() override;

    void cancel() { m_cancel.store(true); }

    /* run() 结束后有效 */
    RhImportExecStats stats;
    bool    failed = false;
    QString error;

signals:
    void progressChanged(int done, int total, const QString &label);

protected:
    void run() override;

private:
    static gboolean cancelCallback(gpointer data);
    static void progressCallback(guint done, guint total, const gchar *name,
                                 gpointer data);

    const RhPlatformRules *m_rules;
    QString m_dbPath;
    QString m_root;
    QStringList m_sources;
    QVector<RhImportPlan *> m_plans;
    gint64 m_jobId;
    bool m_matchDat;
    bool m_generateM3u;
    RhImportNameMode m_nameMode;

    RhStore *m_store = nullptr;
    std::atomic<bool> m_cancel{false};
    std::atomic<int> m_base{0};
    int m_total = 0;
};

#endif /* RH_GUI_IMPORT_JOB_H */
