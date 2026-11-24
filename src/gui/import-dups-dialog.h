/*
 * import-dups-dialog.h —— 导入前重复项处置（忽略/共存/覆盖）
 */
#ifndef RH_GUI_IMPORT_DUPS_DIALOG_H
#define RH_GUI_IMPORT_DUPS_DIALOG_H

#include <QDialog>
#include <QVector>

#include "core-bridge.h"

class QComboBox;
class QTableWidget;

/* 统计一个计划里的重复项数量。 */
int countImportDuplicates(const RhImportPlan *plan);

class ImportDupsDialog : public QDialog {
    Q_OBJECT
public:
    ImportDupsDialog(const QVector<RhImportPlan *> &plans,
                     QWidget *parent = nullptr);

    /* 把界面上的选择写回计划；应在 exec() 返回 Accepted 后调用。 */
    void applyDecisions();

private:
    struct Row {
        RhImportPlanEntry *entry;
        guint              romIndex;
        QComboBox         *action;
    };

    QVector<RhImportPlan *> m_plans;
    QVector<Row>            m_rows;
    QTableWidget            *m_table = nullptr;
};

#endif /* RH_GUI_IMPORT_DUPS_DIALOG_H */
