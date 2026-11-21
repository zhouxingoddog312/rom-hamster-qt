/*
 * dat-dialog.h —— GUI 的 DAT 源管理（与 CLI 的 dat update 对等）
 *
 * 只负责界面与线程编排；下载、校验、索引全部复用核心的 dat-fetch / dat。
 */
#ifndef RH_GUI_DAT_DIALOG_H
#define RH_GUI_DAT_DIALOG_H

#include <atomic>

#include <QDialog>
#include <QString>
#include <QThread>

#include "core-bridge.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QCheckBox;

/*
 * 后台下载线程：一次处理清单里的全部平台。
 * checkOnly 时只比对；否则下载并按 importAfter 决定是否建索引。
 */
class DatFetchThread : public QThread
{
    Q_OBJECT

public:
    DatFetchThread(GPtrArray *sources, const QString &dir, const QString &dbPath,
                   bool checkOnly, bool importAfter, QObject *parent = nullptr);

    void requestCancel();
    bool isCancelled() const;

signals:
    void rowStatus(int row, const QString &text);
    void rowProgress(int row, int percent);
    void rowDone(int row);
    /* checkOnly 时 updated 表示"已是最新"的数量。 */
    void finishedSummary(int updated, int available, int failed,
                         bool cancelled, bool checkOnly);
    void failedToStart(const QString &message);

protected:
    void run() override;

private:
    GPtrArray *m_sources;
    QString m_dir;
    QString m_dbPath;
    bool m_checkOnly;
    bool m_importAfter;
    std::atomic<bool> m_cancel{false};
};

class DatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DatDialog(const QString &dbPath, QWidget *parent = nullptr);
    ~DatDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void checkAll();
    void updateAll();
    void cancel();
    void rowStatus(int row, const QString &text);
    void rowProgress(int row, int percent);
    void rowDone(int row);
    void finishedSummary(int updated, int available, int failed,
                         bool cancelled, bool checkOnly);
    void failedToStart(const QString &message);

private:
    void populate();
    void start(bool checkOnly, bool importAfter);
    void setRunning(bool running);
    void stopThread();

    QString m_dbPath;
    QString m_dir;
    char *m_manifest = nullptr;
    GPtrArray *m_sources = nullptr;
    DatFetchThread *m_thread = nullptr;

    QLabel *m_manifestLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_importCheck = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_updateButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};

#endif /* RH_GUI_DAT_DIALOG_H */
