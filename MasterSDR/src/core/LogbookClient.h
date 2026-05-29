#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

namespace MasterSDR {

struct LogbookQso {
    QString callsign;
    QString band;
    QString mode;
    QString freq;
    QString rstSent;
    QString rstRcvd;
    QString qsoDate;
    QString timeOn;
    QString timeOff;
    QString name;
    QString qth;
    QString grid;
    QString comment;
    QJsonObject toJson() const;
};

class LogbookClient : public QObject {
    Q_OBJECT

public:
    explicit LogbookClient(QObject* parent = nullptr);

    void setApiKey(const QString& key);
    QString apiKey() const { return m_apiKey; }
    bool hasValidKey() const { return !m_apiKey.isEmpty(); }

    void validateKey(std::function<void(bool valid, const QString& message)> callback);
    void sendQso(const LogbookQso& qso, std::function<void(bool ok, const QString& message)> callback);
    void sendQsoBulk(const QJsonArray& qsos, std::function<void(bool ok, const QString& message)> callback);
    void listQsos(std::function<void(bool ok, const QJsonArray& qsos, const QString& message)> callback);
    void getStats(std::function<void(bool ok, const QJsonObject& stats, const QString& message)> callback);
    void deleteQsos(const QJsonArray& ids, std::function<void(bool ok, const QString& message)> callback);

    static const QString BASE_URL;

private:
    QNetworkRequest buildRequest(const QString& path) const;
    void handleReply(QNetworkReply* reply, std::function<void(bool ok, const QJsonDocument& doc, const QString& message)> callback);

    QNetworkAccessManager* m_nam{nullptr};
    QString m_apiKey;
};

} // namespace MasterSDR
