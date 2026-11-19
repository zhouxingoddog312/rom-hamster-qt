/*
 * window-main.h —— 主窗口（ARCHITECTURE.md §11）
 *
 * QTableView + 筛选栏；筛选交给核心 query，按视口分页取数。
 * 拖拽与菜单都走同一条两阶段导入管线。
 */
#ifndef RH_GUI_WINDOW_MAIN_H
#define RH_GUI_WINDOW_MAIN_H

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "core-bridge.h"

class GameTableModel;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(RhStore *store, const QString &dbPath, const QString &root,
               RhPlatformRules *rules, QWidget *parent = nullptr);

    /* 供单实例转发的参数使用：目录直接进入导入流程 */
    void openImportDirectories(const QStringList &paths);
    void activateFromSecondInstance(const QStringList &arguments);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildActions();
    void buildToolBar();
    void refreshPlatforms();
    void updateStatus();
    void openCurrentDetail();
    void chooseAndImport();
    void startImport(const QStringList &directories);

    RhStore        *m_store;
    QString         m_dbPath;
    QString         m_root;
    RhPlatformRules *m_rules;
    bool            m_readOnly = false;
    bool            m_importing = false;

    GameTableModel *m_model = nullptr;
    QTableView     *m_table = nullptr;
    QLineEdit      *m_search = nullptr;
    QComboBox      *m_platformBox = nullptr;
    QComboBox      *m_sortBox = nullptr;
    QCheckBox      *m_descCheck = nullptr;
    QLabel         *m_statusLabel = nullptr;
    QTimer         *m_searchTimer = nullptr;
    QAction        *m_importAction = nullptr;
    QAction        *m_refreshAction = nullptr;
    QAction        *m_detailAction = nullptr;
};

#endif /* RH_GUI_WINDOW_MAIN_H */
