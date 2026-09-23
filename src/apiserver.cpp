#include "apiserver.h"
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>

namespace {
const int kMaxRequestSize = 64 * 1024;

QString reasonPhrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    default:  return "Internal Server Error";
    }
}
}

ApiServer::ApiServer(SatelliteTracker* tracker, QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_tracker(tracker)
{
    connect(m_server, &QTcpServer::newConnection, this, &ApiServer::onNewConnection);
}

bool ApiServer::start(quint16 port) {
    return m_server->listen(QHostAddress::LocalHost, port);
}

void ApiServer::onNewConnection() {
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, &ApiServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void ApiServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray& buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    if (buffer.size() > kMaxRequestSize) {
        sendResponse(socket, error(413, "Request too large"));
        return;
    }

    // Wait until headers and the full body have arrived
    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;

    QList<QByteArray> headerLines = buffer.left(headerEnd).split('\n');
    QList<QByteArray> requestLine = headerLines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        sendResponse(socket, error(400, "Malformed request line"));
        return;
    }

    int contentLength = 0;
    for (int i = 1; i < headerLines.size(); ++i) {
        QByteArray line = headerLines[i].trimmed();
        if (line.toLower().startsWith("content-length:")) {
            contentLength = line.mid(15).trimmed().toInt();
        }
    }

    int bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < contentLength) return;

    QString method = QString::fromLatin1(requestLine[0]).toUpper();
    QUrl url(QString::fromUtf8(requestLine[1]));
    QByteArray body = buffer.mid(bodyStart, contentLength);

    sendResponse(socket, handleRequest(method, url, body));
}

void ApiServer::sendResponse(QTcpSocket* socket, const Response& response) {
    QByteArray payload = response.body.toJson(QJsonDocument::Compact);
    QByteArray header = QString("HTTP/1.1 %1 %2\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: %3\r\n"
                                "Connection: close\r\n\r\n")
                            .arg(response.status)
                            .arg(reasonPhrase(response.status))
                            .arg(payload.size())
                            .toUtf8();
    socket->write(header + payload);
    socket->disconnectFromHost();
    m_buffers.remove(socket);
}

ApiServer::Response ApiServer::error(int status, const QString& message) {
    Response r;
    r.status = status;
    r.body = QJsonDocument(QJsonObject{{"error", message}});
    return r;
}

ApiServer::Response ApiServer::handleRequest(const QString& method, const QUrl& url, const QByteArray& body) {
    QString path = url.path();
    if (path.size() > 1 && path.endsWith('/')) path.chop(1);
    QUrlQuery query(url);

    Response r;

    if (path == "/health") {
        if (method != "GET") return error(405, "Use GET");
        r.body = QJsonDocument(QJsonObject{
            {"status", "ok"},
            {"timeUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"positionsTimeUtc", m_tracker->getPositionsTime().toString(Qt::ISODateWithMs)},
            {"satelliteCount", m_tracker->getAllSatellites().size()},
            {"visibleCount", m_tracker->getVisibleSatellites().size()},
            {"transmitterSatelliteCount", m_tracker->transmitterSatelliteCount()}
        });
        return r;
    }

    if (path == "/observer") {
        if (method == "GET") {
            r.body = QJsonDocument(observerToJson(m_tracker->getObserverLocation()));
            return r;
        }
        if (method == "POST") {
            QJsonParseError parseError;
            QJsonObject obj = QJsonDocument::fromJson(body, &parseError).object();
            if (parseError.error != QJsonParseError::NoError) {
                return error(400, "Invalid JSON: " + parseError.errorString());
            }
            if (!obj.contains("latitude") || !obj.contains("longitude")) {
                return error(400, "latitude and longitude are required");
            }
            double lat = obj["latitude"].toDouble();
            double lon = obj["longitude"].toDouble();
            double alt = obj["altitude"].toDouble(0.0);
            if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
                return error(400, "latitude must be in [-90, 90] and longitude in [-180, 180]");
            }
            m_tracker->setObserverLocation(ObserverLocation(lat, lon, alt));
            r.body = QJsonDocument(observerToJson(m_tracker->getObserverLocation()));
            return r;
        }
        return error(405, "Use GET or POST");
    }

    if (path == "/tle/refresh") {
        if (method != "POST") return error(405, "Use POST");
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString tleUrl = obj["url"].toString();
        if (tleUrl.isEmpty()) {
            m_tracker->fetchTLEData();
        } else {
            m_tracker->fetchTLEData(tleUrl);
        }
        r.status = 202;
        r.body = QJsonDocument(QJsonObject{{"status", "fetch started"}});
        return r;
    }

    if (path == "/satellites" || path == "/satellites/visible") {
        if (method != "GET") return error(405, "Use GET");
        bool visibleOnly = path.endsWith("/visible") || query.queryItemValue("visible") == "true";
        bool includeTrack = query.queryItemValue("groundTrack") == "true";
        QList<Satellite> sats = visibleOnly ? m_tracker->getVisibleSatellites()
                                            : m_tracker->getAllSatellites();
        QJsonArray arr;
        for (const Satellite& sat : sats) {
            arr.append(satelliteToJson(sat, includeTrack));
        }
        r.body = QJsonDocument(QJsonObject{
            {"timeUtc", m_tracker->getPositionsTime().toString(Qt::ISODateWithMs)},
            {"observer", observerToJson(m_tracker->getObserverLocation())},
            {"count", arr.size()},
            {"satellites", arr}
        });
        return r;
    }

    if (path.startsWith("/satellites/")) {
        if (method != "GET") return error(405, "Use GET");
        QString id = QUrl::fromPercentEncoding(path.mid(12).toUtf8());
        bool isNumber = false;
        int catalog = id.toInt(&isNumber);
        for (const Satellite& sat : m_tracker->getAllSatellites()) {
            if ((isNumber && sat.catalogNumber == catalog) ||
                sat.name.compare(id, Qt::CaseInsensitive) == 0) {
                QJsonObject obj = satelliteToJson(sat, true);
                obj["timeUtc"] = m_tracker->getPositionsTime().toString(Qt::ISODateWithMs);
                r.body = QJsonDocument(obj);
                return r;
            }
        }
        return error(404, "Satellite not found: " + id);
    }

    return error(404, "Unknown endpoint: " + path);
}

QJsonObject ApiServer::observerToJson(const ObserverLocation& observer) {
    return QJsonObject{
        {"latitude", observer.latitude},
        {"longitude", observer.longitude},
        {"altitude", observer.altitude}
    };
}

QJsonObject ApiServer::satelliteToJson(const Satellite& sat, bool includeGroundTrack) {
    const SatellitePosition& p = sat.position;
    QJsonObject position{
        {"latitude", p.latitude},
        {"longitude", p.longitude},
        {"altitudeKm", p.altitude},
        {"azimuth", p.azimuth},
        {"elevation", p.elevation},
        {"rangeKm", p.range},
        {"rangeRateKmPerSec", p.rangerate}
    };

    QJsonArray transponders;
    for (const Transponder& t : sat.transponders) {
        transponders.append(QJsonObject{
            {"name", t.name},
            {"uplinkMHz", t.uplinkFreq},
            {"downlinkMHz", t.downlinkFreq},
            {"mode", t.mode},
            {"inverting", t.inverting}
        });
    }

    QJsonObject obj{
        {"name", sat.name},
        {"catalogNumber", sat.catalogNumber},
        {"isVisible", sat.isVisible},
        {"status", p.rangerate < 0 ? "approaching" : "departing"},
        {"position", position},
        {"transponders", transponders},
        {"tle", QJsonArray{sat.line1, sat.line2}}
    };

    if (includeGroundTrack) {
        QJsonArray track;
        for (const QPointF& pt : sat.groundTrack) {
            track.append(QJsonObject{{"azimuth", pt.x()}, {"elevation", pt.y()}});
        }
        obj["groundTrack"] = track;
    }

    return obj;
}
