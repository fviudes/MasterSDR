#include "core/LogbookClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

namespace MasterSDR {

const QString LogbookClient::BASE_URL = QStringLiteral("https://logbook-portal.onrender.com");

QJsonObject LogbookQso::toJson() const
{
    QJsonObject obj;
    if (!callsign.isEmpty()) obj["callsign"] = callsign;
    if (!band.isEmpty())     obj["band"]     = band;
    if (!mode.isEmpty())     obj["mode"]     = mode;
    if (!freq.isEmpty())     obj["freq"]     = freq;
    if (!rstSent.isEmpty())  obj["rst_sent"]  = rstSent;
    if (!rstRcvd.isEmpty())  obj["rst_rcvd"]  = rstRcvd;
    if (!qsoDate.isEmpty())  obj["qso_date"]  = qsoDate;
    if (!timeOn.isEmpty())   obj["time_on"]   = timeOn;
    if (!timeOff.isEmpty())  obj["time_off"]  = timeOff;
    if (!name.isEmpty())     obj["name"]     = name;
    if (!qth.isEmpty())      obj["qth"]      = qth;
    if (!grid.isEmpty())     obj["grid"]     = grid;
    if (!comment.isEmpty())  obj["comment"]  = comment;
    return obj;
}

LogbookClient::LogbookClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void LogbookClient::setApiKey(const QString& key)
{
    m_apiKey = key.trimmed();
}

QNetworkRequest LogbookClient::buildRequest(const QString& path) const
{
    QNetworkRequest request(QUrl(BASE_URL + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    }
    return request;
}

void LogbookClient::handleReply(QNetworkReply* reply,
                                 std::function<void(bool, const QJsonDocument&, const QString&)> callback)
{
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(body);

        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            if (!body.isEmpty()) {
                QJsonObject errObj = doc.object();
                if (errObj.contains("message")) {
                    msg = errObj["message"].toString();
                }
            }
            callback(false, doc, msg);
            return;
        }

        if (statusCode == 401 || statusCode == 403) {
            callback(false, doc, "API key invalida ou nao autorizada. Gere uma nova chave em " + BASE_URL);
            return;
        }

        if (statusCode >= 400) {
            QString msg = QString("Erro HTTP %1").arg(statusCode);
            if (doc.isObject() && doc.object().contains("message")) {
                msg = doc.object()["message"].toString();
            }
            callback(false, doc, msg);
            return;
        }

        callback(true, doc, QString());
    });
}

void LogbookClient::validateKey(std::function<void(bool, const QString&)> callback)
{
    QNetworkRequest request = buildRequest("/api/rest/stats");
    QNetworkReply* reply = m_nam->get(request);

    handleReply(reply, [callback](bool ok, const QJsonDocument& /*doc*/, const QString& message) {
        if (ok) {
            callback(true, "Chave API valida");
        } else {
            callback(false, message.isEmpty() ? "Falha ao validar chave API" : message);
        }
    });
}

void LogbookClient::sendQso(const LogbookQso& qso, std::function<void(bool, const QString&)> callback)
{
    QNetworkRequest request = buildRequest("/api/rest/qso");
    QJsonDocument doc(qso.toJson());
    QNetworkReply* reply = m_nam->post(request, doc.toJson());

    handleReply(reply, [callback](bool ok, const QJsonDocument&, const QString& message) {
        callback(ok, ok ? "QSO enviado com sucesso" : message);
    });
}

void LogbookClient::sendQsoBulk(const QJsonArray& qsos,
                                 std::function<void(bool, const QString&)> callback)
{
    QNetworkRequest request = buildRequest("/api/rest/qso/bulk");
    QJsonDocument doc(qsos);
    QNetworkReply* reply = m_nam->post(request, doc.toJson());

    handleReply(reply, [callback](bool ok, const QJsonDocument&, const QString& message) {
        callback(ok, ok ? "QSOs enviados com sucesso" : message);
    });
}

void LogbookClient::listQsos(std::function<void(bool, const QJsonArray&, const QString&)> callback)
{
    QNetworkRequest request = buildRequest("/api/rest/qso");
    QNetworkReply* reply = m_nam->get(request);

    handleReply(reply, [callback](bool ok, const QJsonDocument& doc, const QString& message) {
        QJsonArray arr;
        if (ok && doc.isArray()) {
            arr = doc.array();
        }
        callback(ok, arr, message);
    });
}

void LogbookClient::getStats(std::function<void(bool, const QJsonObject&, const QString&)> callback)
{
    QNetworkRequest request = buildRequest("/api/rest/stats");
    QNetworkReply* reply = m_nam->get(request);

    handleReply(reply, [callback](bool ok, const QJsonDocument& doc, const QString& message) {
        QJsonObject stats;
        if (ok && doc.isObject()) {
            stats = doc.object();
        }
        callback(ok, stats, message);
    });
}

void LogbookClient::deleteQsos(const QJsonArray& ids,
                                std::function<void(bool, const QString&)> callback)
{
    QNetworkRequest request = buildRequest("/api/rest/qso");
    QJsonDocument doc(ids);
    QNetworkReply* reply = m_nam->deleteResource(request);
    Q_UNUSED(doc);

    if (!ids.isEmpty()) {
        QByteArray body = QJsonDocument(ids).toJson();
        reply->deleteLater();
        reply = m_nam->sendCustomRequest(request, "DELETE", body);
    }

    handleReply(reply, [callback](bool ok, const QJsonDocument&, const QString& message) {
        callback(ok, ok ? "QSOs removidos" : message);
    });
}

} // namespace MasterSDR
