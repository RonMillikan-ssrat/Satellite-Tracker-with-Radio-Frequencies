#ifndef APISERVER_H
#define APISERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonDocument>
#include "satellitetracker.h"
#include "radiocontroller.h"
#include "passpredictor.h"

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
//   GET  /catalogs                   - available catalogs and which one is loaded
//   GET  /catalog                    - the loaded catalog (id, count, data age, loading)
//   POST /catalog                    - load a catalog {"id": "starlink", optional "refresh": true}
//   POST /tle/refresh                - refresh the loaded catalog, or load {"url": ...} instead
//   GET  /passes                     - predicted passes (?hours=24&minElevation=10&satellite=ID&receivableOnly=true)
//   GET  /radio                      - SDR/rigctl connection and tuning status
//   POST /radio/connect              - connect to rigctl (optional {"host","port"})
//   POST /radio/tune                 - Doppler-track a downlink {"satellite", optional "transponder" index,
//                                      optional "startAt" ISO time to arm for a pass instead of tuning now}
//   POST /radio/stop                 - stop tracking
class ApiServer : public QObject {
    Q_OBJECT

public:
    ApiServer(SatelliteTracker* tracker, RadioController* radio, QObject *parent = nullptr);

    bool start(quint16 port);
    quint16 port() const { return m_server->serverPort(); }

    static QJsonObject satelliteToJson(const Satellite& sat, bool includeGroundTrack);
    static QJsonObject observerToJson(const ObserverLocation& observer);
    static QJsonObject radioStatusToJson(const RadioController::Status& status);
    static QJsonObject catalogStateToJson(const SatelliteTracker::CatalogState& state);

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
    RadioController* m_radio;
    QHash<QTcpSocket*, QByteArray> m_buffers;

    bool findSatellite(const QString& id, Satellite* out) const;
    Response handlePassesRequest(const QUrlQuery& query);
    Response handleRadioRequest(const QString& method, const QString& path, const QByteArray& body);
    Response handleRequest(const QString& method, const QUrl& url, const QByteArray& body);
    void sendResponse(QTcpSocket* socket, const Response& response);
    static Response error(int status, const QString& message);
};

#endif // APISERVER_H
