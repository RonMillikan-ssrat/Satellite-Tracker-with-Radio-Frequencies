#ifndef APISERVER_H
#define APISERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonDocument>
#include "satellitetracker.h"

// Minimal local HTTP/JSON API exposing tracker state to external tools
// (e.g. an MCP server). Binds to 127.0.0.1 only.
//
// Endpoints:
//   GET  /health                     - liveness + satellite counts
//   GET  /observer                   - current observer location
//   POST /observer                   - set location {"latitude","longitude","altitude"}
//   GET  /satellites                 - all satellites (?visible=true to filter)
//   GET  /satellites/visible         - satellites above the horizon
//   GET  /satellites/{id}            - one satellite by catalog number or name
//   POST /tle/refresh                - re-download TLE data (optional {"url": ...})
class ApiServer : public QObject {
    Q_OBJECT

public:
    explicit ApiServer(SatelliteTracker* tracker, QObject *parent = nullptr);

    bool start(quint16 port);
    quint16 port() const { return m_server->serverPort(); }

    static QJsonObject satelliteToJson(const Satellite& sat, bool includeGroundTrack);
    static QJsonObject observerToJson(const ObserverLocation& observer);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    struct Response {
        int status = 200;
        QJsonDocument body;
    };

    QTcpServer* m_server;
    SatelliteTracker* m_tracker;
    QHash<QTcpSocket*, QByteArray> m_buffers;

    Response handleRequest(const QString& method, const QUrl& url, const QByteArray& body);
    void sendResponse(QTcpSocket* socket, const Response& response);
    static Response error(int status, const QString& message);
};

#endif // APISERVER_H
