#include "ui-qt.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <new>

namespace {

struct QtUiState {
    QWidget      *parent = nullptr;
    RhQtUiBridge *bridge = nullptr;
    RhUi         *ui = nullptr;
};

void on_log(void *ud, RhLogLevel lv, const char *msg)
{
    Q_UNUSED(ud);
    const char *text = msg != nullptr ? msg : "";
    switch (lv) {
    case RH_LOG_DEBUG:
        qDebug("romham: %s", text);
        break;
    case RH_LOG_INFO:
        qInfo("romham: %s", text);
        break;
    case RH_LOG_WARNING:
        qWarning("romham: %s", text);
        break;
    case RH_LOG_ERROR:
    default:
        qCritical("romham: %s", text);
        break;
    }
}

void on_progress(void *ud, double pct, const char *label)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    if (state == nullptr || state->bridge == nullptr)
        return;
    QString text = label != nullptr ? QString::fromUtf8(label) : QString();
    emit state->bridge->progressChanged(pct, text);
}

int on_confirm(void *ud, const char *title, const char *msg, int default_yes)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    QMessageBox box(state != nullptr ? state->parent : nullptr);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title != nullptr ? QString::fromUtf8(title) : QString());
    box.setText(msg != nullptr ? QString::fromUtf8(msg) : QString());
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(default_yes ? QMessageBox::Yes : QMessageBox::No);
    return box.exec() == QMessageBox::Yes ? 1 : 0;
}

int on_choose(void *ud, const char *title, const char **items, int n,
              const char *remember_key)
{
    Q_UNUSED(remember_key);
    QtUiState *state = static_cast<QtUiState *>(ud);
    if (items == nullptr || n <= 0)
        return -1;

    QStringList list;
    list.reserve(n);
    for (int i = 0; i < n; i++)
        list << QString::fromUtf8(items[i] != nullptr ? items[i] : "");

    bool ok = false;
    QString picked = QInputDialog::getItem(
        state != nullptr ? state->parent : nullptr,
        title != nullptr ? QString::fromUtf8(title) : QString(),
        QString(), list, 0, false, &ok);
    if (!ok)
        return -1;
    return list.indexOf(picked);
}

char *on_enter(void *ud, const char *title, const char *label, const char *def)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    bool ok = false;
    QString text = QInputDialog::getText(
        state != nullptr ? state->parent : nullptr,
        title != nullptr ? QString::fromUtf8(title) : QString(),
        label != nullptr ? QString::fromUtf8(label) : QString(),
        QLineEdit::Normal, def != nullptr ? QString::fromUtf8(def) : QString(), &ok);
    return ok ? g_strdup(text.toUtf8().constData()) : nullptr;
}

char *on_password(void *ud, const char *title, const char *label)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    bool ok = false;
    QString text = QInputDialog::getText(
        state != nullptr ? state->parent : nullptr,
        title != nullptr ? QString::fromUtf8(title) : QString(),
        label != nullptr ? QString::fromUtf8(label) : QString(),
        QLineEdit::Password, QString(), &ok);
    return ok ? g_strdup(text.toUtf8().constData()) : nullptr;
}

GPtrArray *on_pick_files(void *ud, const char *title, int multiple)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    QStringList files = QFileDialog::getOpenFileNames(
        state != nullptr ? state->parent : nullptr,
        title != nullptr ? QString::fromUtf8(title) : QString());
    if (!multiple && files.size() > 1)
        files = files.mid(0, 1);

    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    for (const QString &file : files)
        g_ptr_array_add(result, g_strdup(file.toUtf8().constData()));
    return result;
}

char *on_pick_dir(void *ud, const char *title)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    QString dir = QFileDialog::getExistingDirectory(
        state != nullptr ? state->parent : nullptr,
        title != nullptr ? QString::fromUtf8(title) : QString());
    return dir.isEmpty() ? nullptr : g_strdup(dir.toUtf8().constData());
}

void on_text_view(void *ud, const char *title, const char *text)
{
    QtUiState *state = static_cast<QtUiState *>(ud);
    QDialog dialog(state != nullptr ? state->parent : nullptr);
    dialog.setWindowTitle(title != nullptr ? QString::fromUtf8(title) : QString());
    dialog.resize(720, 520);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QPlainTextEdit *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setPlainText(text != nullptr ? QString::fromUtf8(text) : QString());
    layout->addWidget(view);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

int on_ask_media_kind(void *ud, const char *path, const char *remember_key)
{
    Q_UNUSED(remember_key);
    QtUiState *state = static_cast<QtUiState *>(ud);
    QStringList labels = {QCoreApplication::translate("RhQtUiBridge", "Cover"),
                          QCoreApplication::translate("RhQtUiBridge", "Screenshot"),
                          QCoreApplication::translate("RhQtUiBridge", "Video"),
                          QCoreApplication::translate("RhQtUiBridge", "Marquee"),
                          QCoreApplication::translate("RhQtUiBridge", "Composite Image"),
                          QCoreApplication::translate("RhQtUiBridge", "Description"),
                          QCoreApplication::translate("RhQtUiBridge", "Patch")};
    bool ok = false;
    QString file = path != nullptr ? QString::fromUtf8(path) : QString();
    QString picked = QInputDialog::getItem(
        state != nullptr ? state->parent : nullptr,
        QCoreApplication::translate("RhQtUiBridge", "Media Type"),
        file, labels, 0, false, &ok);
    return ok ? labels.indexOf(picked) : -1;
}

} // namespace

RhQtUiBridge::RhQtUiBridge(QObject *parent) : QObject(parent) {}

RhUi *rh_qt_ui_new(QWidget *parent, RhQtUiBridge **out_bridge)
{
    QtUiState *state = new (std::nothrow) QtUiState;
    if (state == nullptr)
        return nullptr;

    RhUi *ui = new (std::nothrow) RhUi;
    if (ui == nullptr) {
        delete state;
        return nullptr;
    }

    state->parent = parent;
    state->bridge = new RhQtUiBridge();
    state->ui = ui;

    ui->userdata = state;
    ui->log = on_log;
    ui->progress = on_progress;
    ui->confirm = on_confirm;
    ui->choose = on_choose;
    ui->enter = on_enter;
    ui->password = on_password;
    ui->pick_files = on_pick_files;
    ui->pick_dir = on_pick_dir;
    ui->text_view = on_text_view;
    ui->ask_media_kind = on_ask_media_kind;

    if (out_bridge != nullptr)
        *out_bridge = state->bridge;
    return ui;
}

void rh_qt_ui_free(RhUi *ui)
{
    if (ui == nullptr)
        return;
    QtUiState *state = static_cast<QtUiState *>(ui->userdata);
    if (state != nullptr) {
        delete state->bridge;
        delete state;
    }
    delete ui;
}
