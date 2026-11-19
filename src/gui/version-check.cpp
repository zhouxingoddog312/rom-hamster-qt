#include "version-check.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

#include "rh-version.h"

namespace {

QVector<int> versionParts(const QString &version)
{
    QVector<int> parts;
    const QStringList segments = version.split(QLatin1Char('.'));
    for (const QString &segment : segments) {
        int value = 0;
        for (const QChar &ch : segment) {
            if (ch.isDigit())
                value = value * 10 + ch.digitValue();
            else
                break;
        }
        parts.append(value);
    }
    return parts;
}

bool isNewer(const QString &remote, const QString &local)
{
    const QVector<int> a = versionParts(remote);
    const QVector<int> b = versionParts(local);
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int left = i < a.size() ? a.at(i) : 0;
        const int right = i < b.size() ? b.at(i) : 0;
        if (left != right)
            return left > right;
    }
    return false;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
}

void UpdateChecker::check(const QString &url, int intervalHours)
{
    if (url.isEmpty() || m_manager == nullptr)
        return;

    QSettings settings;
    const QDateTime last = QDateTime::fromString(
        settings.value(QStringLiteral("update/lastCheck")).toString(), Qt::ISODate);
    const qint64 interval = static_cast<qint64>(qMax(1, intervalHours)) * 3600;
    if (last.isValid() &&
        last.secsTo(QDateTime::currentDateTimeUtc()) < interval) {
        return;
    }
    settings.setValue(QStringLiteral("update/lastCheck"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(3000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("rom-hamster/%1").arg(QString::fromLatin1(RH_VERSION)));

    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::handleFinished);
}

void UpdateChecker::handleFinished()
{
    if (m_reply == nullptr)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
        return; /* 断网/超时/证书失败：静默 */

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject())
        return;

    const QJsonObject object = doc.object();
    const QString version = object.value(QStringLiteral("version")).toString();
    const QString url = object.value(QStringLiteral("url")).toString();
    if (version.isEmpty() || url.isEmpty())
        return;

    if (isNewer(version, QString::fromLatin1(RH_VERSION)))
        emit updateAvailable(version, url);
}
