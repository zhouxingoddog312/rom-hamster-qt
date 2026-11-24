#include "import-dups-dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

int countImportDuplicates(const RhImportPlan *plan)
{
    if (plan == nullptr)
        return 0;
    int count = 0;
    for (guint i = 0; i < plan->entries->len; i++) {
        RhImportPlanEntry *entry = static_cast<RhImportPlanEntry *>(
            g_ptr_array_index(plan->entries, i));
        if (entry->dups != nullptr)
            count += static_cast<int>(entry->dups->len);
    }
    return count;
}

ImportDupsDialog::ImportDupsDialog(const QVector<RhImportPlan *> &plans,
                                   QWidget *parent)
    : QDialog(parent), m_plans(plans)
{
    setWindowTitle(tr("Duplicate Files Found"));
    resize(900, 520);

    auto *hint = new QLabel(
        tr("These source files look identical to files already in the library. "
           "Choose what to do with each one."),
        this);
    hint->setWordWrap(true);

    auto *bulk = new QComboBox(this);
    bulk->addItem(tr("Ignore"), static_cast<int>(RH_DUP_IGNORE));
    bulk->addItem(tr("Keep both (rename)"), static_cast<int>(RH_DUP_COEXIST));
    bulk->addItem(tr("Overwrite existing"), static_cast<int>(RH_DUP_OVERWRITE));
    auto *applyAll = new QPushButton(tr("Apply to all"), this);

    auto *bulkRow = new QHBoxLayout();
    bulkRow->addWidget(new QLabel(tr("Set all to"), this));
    bulkRow->addWidget(bulk);
    bulkRow->addWidget(applyAll);
    bulkRow->addStretch(1);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("Source"), tr("Existing file"), tr("Size"), tr("Similarity"),
         tr("Action")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    for (RhImportPlan *plan : m_plans) {
        if (plan == nullptr)
            continue;
        for (guint i = 0; i < plan->entries->len; i++) {
            RhImportPlanEntry *entry = static_cast<RhImportPlanEntry *>(
                g_ptr_array_index(plan->entries, i));
            if (entry->dups == nullptr)
                continue;
            for (guint j = 0; j < entry->dups->len; j++) {
                RhImportDup *dup = static_cast<RhImportDup *>(
                    g_ptr_array_index(entry->dups, j));
                const int row = m_table->rowCount();
                m_table->insertRow(row);

                QString source = entry->name != nullptr
                    ? QString::fromUtf8(entry->name) : QString();
                if (dup->rom_index < entry->roms->len) {
                    const char *file = static_cast<const char *>(
                        g_ptr_array_index(entry->roms, dup->rom_index));
                    source += QStringLiteral(" / ") + QString::fromUtf8(file);
                }

                m_table->setItem(row, 0, new QTableWidgetItem(source));
                m_table->setItem(row, 1, new QTableWidgetItem(
                    QString::fromUtf8(dup->existing_rel_path)));
                m_table->setItem(row, 2, new QTableWidgetItem(
                    QString::number(dup->size)));
                m_table->setItem(row, 3, new QTableWidgetItem(
                    dup->same_name
                        ? tr("Same name (%1%)").arg(dup->similarity)
                        : QStringLiteral("%1%").arg(dup->similarity)));

                auto *action = new QComboBox(m_table);
                action->addItem(tr("Ignore"), static_cast<int>(RH_DUP_IGNORE));
                action->addItem(tr("Keep both"), static_cast<int>(RH_DUP_COEXIST));
                action->addItem(tr("Overwrite"), static_cast<int>(RH_DUP_OVERWRITE));
                action->setCurrentIndex(static_cast<int>(dup->action));
                m_table->setCellWidget(row, 4, action);

                m_rows.append(Row{entry, dup->rom_index, action});
            }
        }
    }
    m_table->resizeColumnsToContents();

    connect(applyAll, &QPushButton::clicked, this, [this, bulk]() {
        const int value = bulk->currentData().toInt();
        for (const Row &row : m_rows)
            row.action->setCurrentIndex(value);
    });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addLayout(bulkRow);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);
}

void ImportDupsDialog::applyDecisions()
{
    for (const Row &row : m_rows) {
        const int value = row.action->currentData().toInt();
        rh_import_dup_set_action(row.entry, row.romIndex,
                                 static_cast<RhDupAction>(value));
    }
}
