/*
 * version-check.h —— 启动时的版本检查（ARCHITECTURE.md §11）
 *
 * 异步 GET 更新清单 JSON（{"version":"x.y.z","url":"..."}），3 秒超时；
 * 断网/失败一律静默，不阻塞启动，不下载、不安装。
 * 结果按间隔缓存在 QSettings，避免每次启动都请求。
 */
#ifndef RH_GUI_VERSION_CHECK_H
#define RH_GUI_VERSION_CHECK_H

#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    /* url 为空或距上次检查不足 intervalHours 时直接返回 */
    void check(const QString &url, int intervalHours);

signals:
    void updateAvailable(const QString &version, const QString &url);

private:
    void handleFinished();

    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply         *m_reply = nullptr;
};

#endif /* RH_GUI_VERSION_CHECK_H */
