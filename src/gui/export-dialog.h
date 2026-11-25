/*
 * export-dialog.h —— 导出期：按前端规格生成清单并复制 ROM/媒体
 *
 * 核心导出（export.h）是只读的；这里只负责选规格、选输出目录、传选项，
 * 作业放到独立线程里跑，避免复制大文件时冻结界面。
 */
#ifndef RH_GUI_EXPORT_DIALOG_H
#define RH_GUI_EXPORT_DIALOG_H

#include <QDialog>
#include <QString>
#include <QThread>

#include "core-bridge.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QProgressDialog;
class QPushButton;

/* 在独立线程里打开自己的库连接，避免跨线程共用 sqlite3。 */
class ExportThread : public QThread {
    Q_OBJECT
public:
    ExportThread(const QString &dbPath, const QString &root,
                 const RhPlatformRules *rules, const QString &specPath,
                 const QString &outputDir, bool copyRoms, bool copyMedia,
                 bool generateM3u, RhExportNameMode nameMode,
                 QObject *parent = nullptr);

    RhExportStats stats{};
    bool failed = false;
    QString error;

signals:
    void progressChanged(int done, int total, const QString &name);

protected:
    void run() override;

private:
    static void progressCallback(guint done, guint total, const char *name,
                                 gpointer user_data);

    QString m_dbPath;
    QString m_root;
    const RhPlatformRules *m_rules;
    QString m_specPath;
    QString m_outputDir;
    bool m_copyRoms;
    bool m_copyMedia;
    bool m_generateM3u;
    RhExportNameMode m_nameMode;
};

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(const QString &dbPath, const QString &root,
                 const RhPlatformRules *rules, QWidget *parent = nullptr);
    ~ExportDialog() override;

private:
    void chooseSpec();
    void chooseOutput();
    void startExport();
    void finishExport();

    QString m_dbPath;
    QString m_root;
    const RhPlatformRules *m_rules;

    QComboBox *m_spec = nullptr;
    QLineEdit *m_output = nullptr;
    QCheckBox *m_copyRoms = nullptr;
    QCheckBox *m_copyMedia = nullptr;
    QCheckBox *m_m3u = nullptr;
    QComboBox *m_names = nullptr;
    QPushButton *m_export = nullptr;
    QProgressDialog *m_progress = nullptr;
    ExportThread *m_thread = nullptr;
    bool m_running = false;
};

#endif /* RH_GUI_EXPORT_DIALOG_H */
