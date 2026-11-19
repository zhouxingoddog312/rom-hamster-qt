/*
 * ui-qt.h —— rh_ui 的 Qt 实现（ARCHITECTURE.md §6.1）
 *
 * 仅供主线程使用：确认、选择、输入都直接弹 Qt 对话框。
 * progress 通过信号转发，允许工作线程调用（AutoConnection 会排队到主线程）。
 */
#ifndef RH_GUI_UI_QT_H
#define RH_GUI_UI_QT_H

#include <QObject>
#include <QString>

#include "core-bridge.h"

class QWidget;

class RhQtUiBridge : public QObject {
    Q_OBJECT
public:
    explicit RhQtUiBridge(QObject *parent = nullptr);

signals:
    void progressChanged(double pct, const QString &label);
};

/*
 * 创建一份 RhUi。parent 只作为对话框的父窗口，不取得所有权；
 * 调用方用 rh_qt_ui_free 释放（bridge 随之删除）。
 */
RhUi *rh_qt_ui_new(QWidget *parent, RhQtUiBridge **out_bridge);
void rh_qt_ui_free(RhUi *ui);

#endif /* RH_GUI_UI_QT_H */
