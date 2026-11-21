#include "window-detail.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRunnable>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1)
        .arg(QString::fromLatin1(units[unit]));
}

bool isVideoFile(const QString &filename)
{
    const QString suffix = QFileInfo(filename).suffix().toLower();
    static const QStringList videoSuffixes = {"mp4", "avi", "mkv", "webm", "mov", "m4v"};
    return videoSuffixes.contains(suffix);
}

bool isImageFile(const QString &filename)
{
    const QString suffix = QFileInfo(filename).suffix().toLower();
    static const QStringList imageSuffixes = {"png", "jpg", "jpeg", "webp", "bmp", "gif"};
    return imageSuffixes.contains(suffix);
}

/*
 * 缩略图：缓存键 = 路径 + mtime + 目标尺寸，落在
 * QStandardPaths::CacheLocation/thumbs（§11）。
 */
class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(QString path, QSize size)
        : m_path(std::move(path)), m_size(std::move(size))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QImage image;
        const QFileInfo info(m_path);
        const QString key = QStringLiteral("%1|%2|%3x%4")
                                .arg(m_path)
                                .arg(info.lastModified().toMSecsSinceEpoch())
                                .arg(m_size.width())
                                .arg(m_size.height());
        const QByteArray digest =
            QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex();

        const QString cacheDir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/thumbs");
        const QString cachePath = cacheDir + QLatin1Char('/') + QString::fromLatin1(digest)
                                  + QStringLiteral(".png");

        if (QFileInfo::exists(cachePath) && image.load(cachePath)) {
            emit ready(m_path, image);
            return;
        }

        if (image.isNull()) {
            QImageReader reader(m_path);
            reader.setAutoTransform(true);
            const QSize original = reader.size();
            if (original.isValid()) {
                QSize scaled = original;
                scaled.scale(m_size, Qt::KeepAspectRatio);
                if (!scaled.isEmpty())
                    reader.setScaledSize(scaled);
            }
            image = reader.read();
            if (!image.isNull() && QDir().mkpath(cacheDir))
                image.save(cachePath, "PNG");
        }

        emit ready(m_path, image);
    }

signals:
    void ready(const QString &path, const QImage &image);

private:
    QString m_path;
    QSize   m_size;
};

} // namespace

DetailWindow::DetailWindow(RhStore *store, const QString &root, const QString &gameId,
                           QWidget *parent)
    : QDialog(parent), m_root(root)
{
    setWindowTitle(tr("Game Details"));
    resize(980, 760);

    RhErr err = RH_OK;
    m_detail = rh_detail_load(store, gameId.toUtf8().constData(), FALSE, &err);

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *title = new QLabel(this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    if (m_detail == nullptr) {
        title->setText(tr("Unable to Load Game Details"));
        QLabel *reason = new QLabel(
            tr("Error: %1").arg(QString::fromUtf8(rh_err_str(err))), this);
        layout->addWidget(reason);
        return;
    }

    const RhGame *game = m_detail->game;
    title->setText(QString::fromUtf8(game->name != nullptr ? game->name : ""));
    m_dirPath = QString::fromUtf8(game->dir_path != nullptr ? game->dir_path : "");

    QGroupBox *meta = new QGroupBox(tr("Metadata"), this);
    QFormLayout *form = new QFormLayout(meta);
    auto addRow = [form](const QString &label, const QString &value) {
        if (value.isEmpty())
            return;
        form->addRow(label, new QLabel(value));
    };

    addRow(tr("Platform"), QString::fromUtf8(game->platform != nullptr ? game->platform : ""));
    addRow(tr("Developer"), QString::fromUtf8(game->developer != nullptr ? game->developer : ""));
    addRow(tr("Publisher"), QString::fromUtf8(game->publisher != nullptr ? game->publisher : ""));
    addRow(tr("Year"), game->year > 0 ? QString::number(game->year) : QString());
    addRow(tr("Genre"), QString::fromUtf8(game->genre != nullptr ? game->genre : ""));
    addRow(tr("Players"), game->players > 0 ? QString::number(game->players) : QString());
    addRow(tr("Region"), QString::fromUtf8(game->region != nullptr ? game->region : ""));
    addRow(tr("Rating"), game->rating > 0.0 ? QString::number(game->rating, 'f', 2) : QString());
    addRow(tr("ROM Format"), QString::fromUtf8(game->rom_format != nullptr ? game->rom_format : ""));
    addRow(tr("ROM Status"), QString::fromUtf8(game->rom_status != nullptr ? game->rom_status : ""));
    addRow(tr("DAT Source"), QString::fromUtf8(game->dat_source != nullptr ? game->dat_source : ""));
    addRow(tr("Patch Type"), QString::fromUtf8(game->patch_type != nullptr ? game->patch_type : ""));
    addRow(tr("Multi-disc"), game->is_multi_disc ? tr("Yes") : QString());
    addRow(tr("Arcade"), game->is_arcade ? tr("Yes") : QString());
    addRow(tr("Directory"), m_dirPath);
    addRow(tr("ROM Size"), formatBytes(m_detail->rom_bytes));
    addRow(tr("Media Size"), formatBytes(m_detail->media_bytes));
    layout->addWidget(meta);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildRomTab(), tr("ROM Files"));
    m_tabs->addTab(buildMediaTab(QStringLiteral("covers"), false), tr("Covers"));
    m_tabs->addTab(buildMediaTab(QStringLiteral("screenshots"), false), tr("Screenshots"));
    m_tabs->addTab(buildMediaTab(QStringLiteral("videos"), true), tr("Videos"));
    m_tabs->addTab(buildMediaTab(QStringLiteral("marquees"), false), tr("Marquees"));
    m_tabs->addTab(buildMediaTab(QStringLiteral("miximages"), false), tr("Composite Images"));
    m_tabs->addTab(buildDescriptionTab(), tr("Description"));
    m_tabs->addTab(buildMediaTab(QStringLiteral("patcher"), false), tr("Patches"));
    layout->addWidget(m_tabs, 1);
}

DetailWindow::~DetailWindow()
{
    if (m_detail != nullptr)
        rh_detail_free(m_detail);
}

QString DetailWindow::filePath(const QString &filename) const
{
    return QDir(m_root).filePath(
        m_dirPath.isEmpty() ? filename : m_dirPath + QLatin1Char('/') + filename);
}

QWidget *DetailWindow::buildRomTab()
{
    QTableWidget *table = new QTableWidget(m_detail->roms->len, 6, this);
    table->setHorizontalHeaderLabels({tr("Filename"), tr("Size"), tr("Disc No."),
                                      QStringLiteral("CRC32"), QStringLiteral("MD5"),
                                      QStringLiteral("SHA1")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);

    for (guint i = 0; i < m_detail->roms->len; i++) {
        const RhRomFile *rom =
            static_cast<const RhRomFile *>(g_ptr_array_index(m_detail->roms, i));
        const int row = static_cast<int>(i);
        table->setItem(row, 0, new QTableWidgetItem(
            QString::fromUtf8(rom->filename != nullptr ? rom->filename : "")));
        table->setItem(row, 1, new QTableWidgetItem(
            rom->has_size ? formatBytes(rom->size) : QString()));
        table->setItem(row, 2, new QTableWidgetItem(
            rom->disc_no > 0 ? QString::number(rom->disc_no) : QString()));
        table->setItem(row, 3, new QTableWidgetItem(
            QString::fromUtf8(rom->crc32 != nullptr ? rom->crc32 : "")));
        table->setItem(row, 4, new QTableWidgetItem(
            QString::fromUtf8(rom->md5 != nullptr ? rom->md5 : "")));
        table->setItem(row, 5, new QTableWidgetItem(
            QString::fromUtf8(rom->sha1 != nullptr ? rom->sha1 : "")));
    }
    table->resizeColumnsToContents();
    return table;
}

QWidget *DetailWindow::buildMediaTab(const QString &kind, bool externalPlayer)
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QSplitter *splitter = new QSplitter(Qt::Vertical, page);
    QListWidget *list = new QListWidget(splitter);
    QLabel *preview = new QLabel(splitter);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(220);
    preview->setText(tr("Select a file to preview"));
    preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    splitter->addWidget(list);
    splitter->addWidget(preview);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    QStringList files;
    for (guint i = 0; i < m_detail->media->len; i++) {
        const RhMediaFile *media =
            static_cast<const RhMediaFile *>(g_ptr_array_index(m_detail->media, i));
        if (g_strcmp0(media->kind, kind.toUtf8().constData()) != 0)
            continue;
        const QString filename =
            QString::fromUtf8(media->filename != nullptr ? media->filename : "");
        files.append(filename);
        QListWidgetItem *item = new QListWidgetItem(filename, list);
        item->setToolTip(filename);
    }

    if (files.isEmpty()) {
        preview->setText(tr("(None)"));
        return page;
    }

    QPushButton *open = new QPushButton(
        externalPlayer ? tr("Open with External Player") : tr("Open with System Application"),
        page);
    open->setEnabled(false);
    layout->addWidget(open, 0, Qt::AlignRight);

    QObject::connect(list, &QListWidget::currentTextChanged, this,
                     [this, preview, open](const QString &filename) {
                         open->setEnabled(!filename.isEmpty());
                         if (filename.isEmpty())
                             return;
                         const QString path = filePath(filename);
                         if (isVideoFile(filename) || !isImageFile(filename)) {
                             preview->setPixmap(QPixmap());
                             preview->setText(tr("Not an image file: %1").arg(path));
                             return;
                         }
                         preview->setText(tr("Loading…"));
                         ThumbnailTask *task =
                             new ThumbnailTask(path, QSize(720, 480));
                         QObject::connect(task, &ThumbnailTask::ready, this,
                                          [this, preview, path](const QString &loaded,
                                                                const QImage &image) {
                                              if (loaded != path)
                                                  return;
                                              if (image.isNull()) {
                                                  preview->setText(
                                                      tr("Unable to decode: %1").arg(path));
                                                  return;
                                              }
                                              preview->setPixmap(QPixmap::fromImage(image));
                                          });
                         QThreadPool::globalInstance()->start(task);
                     });

    QObject::connect(open, &QPushButton::clicked, this, [this, list]() {
        const QListWidgetItem *item = list->currentItem();
        if (item != nullptr)
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath(item->text())));
    });

    return page;
}

QWidget *DetailWindow::buildDescriptionTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    QPlainTextEdit *text = new QPlainTextEdit(page);
    text->setReadOnly(true);
    const gchar *description = m_detail->game->description;
    text->setPlainText(description != nullptr && *description != '\0'
                           ? QString::fromUtf8(description)
                           : tr("(No description)"));
    layout->addWidget(text, 1);

    QStringList introFiles;
    for (guint i = 0; i < m_detail->media->len; i++) {
        const RhMediaFile *media =
            static_cast<const RhMediaFile *>(g_ptr_array_index(m_detail->media, i));
        if (g_strcmp0(media->kind, "intro") == 0 && media->filename != nullptr)
            introFiles.append(QString::fromUtf8(media->filename));
    }
    if (!introFiles.isEmpty()) {
        QListWidget *list = new QListWidget(page);
        list->addItems(introFiles);
        QObject::connect(list, &QListWidget::itemDoubleClicked, this,
                         [this](QListWidgetItem *item) {
                             if (item != nullptr)
                                 QDesktopServices::openUrl(
                                     QUrl::fromLocalFile(filePath(item->text())));
                         });
        layout->addWidget(list);
    }

    return page;
}

#include "window-detail.moc"
