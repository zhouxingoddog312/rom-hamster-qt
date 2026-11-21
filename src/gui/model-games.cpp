#include "model-games.h"

#include <cstring>

GameTableModel::GameTableModel(RhStore *store, QObject *parent)
    : QAbstractTableModel(parent), m_store(store)
{
}

GameTableModel::~GameTableModel()
{
    for (RhQueryResult *result : m_pages)
        rh_query_result_free(result);
}

void GameTableModel::setKeyword(const QString &keyword)
{
    if (m_keyword == keyword)
        return;
    m_keyword = keyword;
    reload();
}

void GameTableModel::setPlatform(const QString &platformId)
{
    if (m_platform == platformId)
        return;
    m_platform = platformId;
    reload();
}

void GameTableModel::setSort(const QString &sort, bool descending)
{
    if (m_sort == sort && m_desc == descending)
        return;
    m_sort = sort;
    m_desc = descending;
    reload();
}

void GameTableModel::reload()
{
    beginResetModel();
    for (RhQueryResult *result : m_pages)
        rh_query_result_free(result);
    m_pages.clear();
    m_total = 0;
    endResetModel();

    if (m_store == nullptr) {
        emit errorOccurred(tr("Database is not open"));
        return;
    }

    RhQueryResult *page = runQuery(0);
    if (page == nullptr)
        return;
    const int count = static_cast<int>(page->games->len);
    if (count == 0) {
        rh_query_result_free(page);
        return;
    }

    beginInsertRows(QModelIndex(), 0, count - 1);
    m_pages.append(page);
    endInsertRows();
}

RhQueryResult *GameTableModel::runQuery(int offset)
{
    RhFilter filter;
    std::memset(&filter, 0, sizeof(filter));
    filter.multi = -1;
    filter.has_patch = -1;
    filter.limit = kPageSize;
    filter.offset = offset;
    filter.desc = m_desc ? 1 : 0;
    /* QByteArray 必须在 rh_query_run 返回前保持存活，不能取临时对象的指针 */
    const QByteArray sortUtf8 = m_sort.toUtf8();
    const QByteArray platformUtf8 = m_platform.toUtf8();
    const QByteArray keywordUtf8 = m_keyword.toUtf8();
    filter.sort = m_sort.isEmpty() ? nullptr : sortUtf8.constData();
    filter.platform = m_platform.isEmpty() ? nullptr : platformUtf8.constData();
    filter.kw = m_keyword.isEmpty() ? nullptr : keywordUtf8.constData();

    RhErr err = RH_OK;
    RhQueryResult *result = rh_query_run(m_store, &filter, &err);
    if (result == nullptr) {
        emit errorOccurred(QString::fromUtf8(rh_err_str(err)));
        return nullptr;
    }
    m_total = result->total;
    return result;
}

int GameTableModel::loadedCount() const
{
    int count = 0;
    for (const RhQueryResult *result : m_pages)
        count += static_cast<int>(result->games->len);
    return count;
}

const RhGame *GameTableModel::gameAt(int row) const
{
    if (row < 0)
        return nullptr;
    int offset = row;
    for (const RhQueryResult *result : m_pages) {
        const int len = static_cast<int>(result->games->len);
        if (offset < len)
            return static_cast<const RhGame *>(g_ptr_array_index(result->games, offset));
        offset -= len;
    }
    return nullptr;
}

int GameTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return loadedCount();
}

int GameTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant GameTableModel::data(const QModelIndex &index, int role) const
{
    const RhGame *game = gameAt(index.row());
    if (game == nullptr)
        return QVariant();

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColYear:
        case ColRating:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role != Qt::DisplayRole)
        return QVariant();

    switch (index.column()) {
    case ColName:
        return QString::fromUtf8(game->name != nullptr ? game->name : "");
    case ColPlatform:
        return QString::fromUtf8(game->platform != nullptr ? game->platform : "");
    case ColYear:
        return game->year > 0 ? QString::number(game->year) : QString();
    case ColGenre:
        return QString::fromUtf8(game->genre != nullptr ? game->genre : "");
    case ColDeveloper:
        return QString::fromUtf8(game->developer != nullptr ? game->developer : "");
    case ColPublisher:
        return QString::fromUtf8(game->publisher != nullptr ? game->publisher : "");
    case ColRegion:
        return QString::fromUtf8(game->region != nullptr ? game->region : "");
    case ColRating:
        return game->rating > 0.0 ? QString::number(game->rating, 'f', 1) : QString();
    case ColDiscs:
        return game->is_multi_disc ? tr("Multi-disc") : QString();
    case ColStatus:
        return QString::fromUtf8(game->rom_status != nullptr ? game->rom_status : "");
    default:
        return QVariant();
    }
}

QVariant GameTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColName:      return tr("Name");
    case ColPlatform:  return tr("Platform");
    case ColYear:      return tr("Year");
    case ColGenre:     return tr("Genre");
    case ColDeveloper: return tr("Developer");
    case ColPublisher: return tr("Publisher");
    case ColRegion:    return tr("Region");
    case ColRating:    return tr("Rating");
    case ColDiscs:     return tr("Discs");
    case ColStatus:    return tr("ROM Status");
    default:           return QVariant();
    }
}

bool GameTableModel::canFetchMore(const QModelIndex &parent) const
{
    if (parent.isValid())
        return false;
    return loadedCount() < m_total;
}

void GameTableModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid() || loadedCount() >= m_total)
        return;

    const int first = loadedCount();
    RhQueryResult *page = runQuery(first);
    if (page == nullptr)
        return;
    const int count = static_cast<int>(page->games->len);
    if (count == 0) {
        rh_query_result_free(page);
        return;
    }

    beginInsertRows(QModelIndex(), first, first + count - 1);
    m_pages.append(page);
    endInsertRows();
}
