/*
 * window-detail.h —— 游戏详情（ARCHITECTURE.md §11）
 *
 * 元数据 + 8 类资源分页：ROM 文件 / 封面 / 截图 / 视频 / 横幅 / 合成图 /
 * 说明 / 补丁。图片在工作线程解码并写磁盘缩略图缓存；视频默认外部播放器。
 */
#ifndef RH_GUI_WINDOW_DETAIL_H
#define RH_GUI_WINDOW_DETAIL_H

#include <QDialog>
#include <QString>

#include "core-bridge.h"

class QLabel;
class QTabWidget;
class QVBoxLayout;

class DetailWindow : public QDialog {
    Q_OBJECT
public:
    DetailWindow(RhStore *store, const QString &root, const QString &gameId,
                 QWidget *parent = nullptr);
    ~DetailWindow() override;

private:
    QWidget *buildRomTab();
    QWidget *buildMediaTab(const QString &kind, bool externalPlayer);
    QWidget *buildDescriptionTab();
    QString  filePath(const QString &filename) const;

    RhGameDetail *m_detail = nullptr;
    QString       m_root;
    QString       m_dirPath;
    QTabWidget   *m_tabs = nullptr;
};

#endif /* RH_GUI_WINDOW_DETAIL_H */
