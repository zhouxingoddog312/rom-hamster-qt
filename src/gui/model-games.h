/*
 * model-games.h —— 主窗口列表的数据模型（ARCHITECTURE.md §11）
 *
 * 按需分页：每页 200 条，由 rh_query_run 读取；行数增长时才取下一页，
 * 不在 GUI 侧全量过滤。筛选条件全部交给核心 SQL。
 */
#ifndef RH_GUI_MODEL_GAMES_H
#define RH_GUI_MODEL_GAMES_H

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

#include "core-bridge.h"

class GameTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColPlatform,
        ColYear,
        ColGenre,
        ColDeveloper,
        ColPublisher,
        ColRegion,
        ColRating,
        ColDiscs,
        ColStatus,
        ColumnCount
    };

    explicit GameTableModel(RhStore *store, QObject *parent = nullptr);
    ~GameTableModel() override;

    void setKeyword(const QString &keyword);
    void setPlatform(const QString &platformId);
    void setSort(const QString &sort, bool descending);
    void reload();

    qint64 total() const { return m_total; }
    const RhGame *gameAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

signals:
    void errorOccurred(const QString &message);

private:
    static constexpr int kPageSize = 200;

    RhQueryResult *runQuery(int offset);
    int loadedCount() const;

    RhStore *m_store = nullptr;
    QString  m_keyword;
    QString  m_platform;
    QString  m_sort = QStringLiteral("name");
    bool     m_desc = false;

    QVector<RhQueryResult *> m_pages;
    qint64 m_total = 0;
};

#endif /* RH_GUI_MODEL_GAMES_H */
