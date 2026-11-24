/*
 * dedup-dialog.h —— 重复文件列表；支持选中后移入回收站并删除库内记录
 */
#ifndef RH_GUI_DEDUP_DIALOG_H
#define RH_GUI_DEDUP_DIALOG_H

#include <QDialog>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>

#include "core-bridge.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

/* 在独立线程里打开自己的库连接，避免跨线程共用 sqlite3。 */
class DedupThread : public QThread {
    Q_OBJECT
public:
    DedupThread(const QString &dbPath, const QString &root,
                const QString &platform, bool includeMedia, bool backfill,
                qint64 minSize, QObject *parent = nullptr);

    GPtrArray *groups = nullptr;   /* RhDupGroup*，run() 后归调用方 */
    RhDedupStats stats;
    bool failed = false;
    QString error;

    void cancel() { m_cancel = true; }

signals:
    void progressChanged(int done, int total, const QString &path);

protected:
    void run() override;

private:
    static gboolean progressCallback(const char *path, guint done, guint total,
                                     gpointer user_data);

    QString m_dbPath;
    QString m_root;
    QString m_platform;
    bool m_includeMedia;
    bool m_backfill;
    qint64 m_minSize;
    std::atomic<bool> m_cancel{false};
};

class DedupDialog : public QDialog {
    Q_OBJECT
public:
    DedupDialog(const QString &dbPath, const QString &root,
                QWidget *parent = nullptr);
    ~DedupDialog() override;

private:
    struct Row {
        QString path;       /* 磁盘绝对路径 */
        QString dirPath;    /* 库内相对目录 */
        QString filename;
        QString kind;       /* 空 = ROM */
        bool    missing = false;
    };

    void reload();
    void showGroups(GPtrArray *groups, const RhDedupStats &stats);
    void showPlatforms();
    void deleteSelected();

    QString m_dbPath;
    QString m_root;
    QComboBox *m_platform = nullptr;
    QCheckBox *m_media = nullptr;
    QCheckBox *m_backfill = nullptr;
    QComboBox *m_minSize = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_delete = nullptr;
    QVector<Row> m_rows;
    DedupThread *m_thread = nullptr;
    bool m_running = false;
};

#endif /* RH_GUI_DEDUP_DIALOG_H */
