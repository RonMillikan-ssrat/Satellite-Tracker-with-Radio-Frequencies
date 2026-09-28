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

ApiServer::ApiServer(SatelliteTracker* tracker, RadioController* radio, QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_tracker(tracker)
    , m_radio(radio)
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
        Satellite sat;
        if (!findSatellite(id, &sat)) return error(404, "Satellite not found: " + id);
        QJsonObject obj = satelliteToJson(sat, true);
        obj["timeUtc"] = m_tracker->getPositionsTime().toString(Qt::ISODateWithMs);
        r.body = QJsonDocument(obj);
        return r;
    }

    if (path == "/passes") {
        if (method != "GET") return error(405, "Use GET");
        return handlePassesRequest(query);
    }

    if (path == "/radio" || path.startsWith("/radio/")) {
        return handleRadioRequest(method, path, body);
    }

    return error(404, "Unknown endpoint: " + path);
}

bool ApiServer::findSatellite(const QString& id, Satellite* out) const {
    bool isNumber = false;
    int catalog = id.toInt(&isNumber);
    for (const Satellite& sat : m_tracker->getAllSatellites()) {
        if ((isNumber && sat.catalogNumber == catalog) ||
            sat.name.compare(id, Qt::CaseInsensitive) == 0) {
            *out = sat;
            return true;
        }
    }
    return false;
}

ApiServer::Response ApiServer::handleRadioRequest(const QString& method, const QString& path, const QByteArray& body) {
    Response r;

    if (path == "/radio") {
        if (method != "GET") return error(405, "Use GET");
        r.body = QJsonDocument(radioStatusToJson(m_radio->status()));
        return r;
    }

    if (method != "POST") return error(405, "Use POST");
    QJsonParseError parseError;
    QJsonObject obj = QJsonDocument::fromJson(body, &parseError).object();
    if (!body.trimmed().isEmpty() && parseError.error != QJsonParseError::NoError) {
        return error(400, "Invalid JSON: " + parseError.errorString());
    }

    if (path == "/radio/connect") {
        RadioController::Status s = m_radio->status();
        int port = obj["port"].toInt(s.port);
        if (port <= 0 || port > 65535) return error(400, "port must be in [1, 65535]");
        m_radio->setServer(obj["host"].toString(s.host), quint16(port));
        m_radio->connectToRadio();
        r.status = 202;
        r.body = QJsonDocument(radioStatusToJson(m_radio->status()));
        return r;
    }

    if (path == "/radio/tune") {
        QString id = obj["satellite"].isDouble() ? QString::number(obj["satellite"].toInt())
                                                 : obj["satellite"].toString();
        if (id.isEmpty()) return error(400, "satellite (name or catalog number) is required");
        Satellite sat;
        if (!findSatellite(id, &sat)) return error(404, "Satellite not found: " + id);
        int index = obj.contains("transponder") ? obj["transponder"].toInt(-1)
                                                : m_radio->defaultTransponder(sat);
        if (index < 0 && !obj.contains("transponder")) {
            return error(400, sat.name + " has no downlink the receiver can tune");
        }
        QString failure;
        if (obj.contains("startAt")) {
            QDateTime startAt = QDateTime::fromString(obj["startAt"].toString(), Qt::ISODateWithMs);
            if (!startAt.isValid()) return error(400, "startAt must be an ISO 8601 time, e.g. a pass's aosUtc");
            failure = m_radio->armForPass(sat, index, startAt);
        } else {
            failure = m_radio->startTracking(sat, index);
        }
        if (!failure.isEmpty()) return error(400, failure);
        r.body = QJsonDocument(radioStatusToJson(m_radio->status()));
        return r;
    }

    if (path == "/radio/stop") {
        m_radio->stopTracking();
        r.body = QJsonDocument(radioStatusToJson(m_radio->status()));
        return r;
    }

    return error(404, "Unknown endpoint: " + path);
}

ApiServer::Response ApiServer::handlePassesRequest(const QUrlQuery& query) {
    bool ok = true;
    double hours = query.hasQueryItem("hours") ? query.queryItemValue("hours").toDouble(&ok) : 24.0;
    if (!ok || hours <= 0 || hours > 168) return error(400, "hours must be in (0, 168]");
    double minElevation = query.hasQueryItem("minElevation")
        ? query.queryItemValue("minElevation").toDouble(&ok) : 10.0;
    if (!ok || minElevation < 0 || minElevation > 90) return error(400, "minElevation must be in [0, 90]");
    bool receivableOnly = query.queryItemValue("receivableOnly") == "true";

    QList<Satellite> satellites;
    QString id = query.queryItemValue("satellite", QUrl::FullyDecoded);
    if (!id.isEmpty()) {
        Satellite sat;
        if (!findSatellite(id, &sat)) return error(404, "Satellite not found: " + id);
        satellites.append(sat);
    } else {
        satellites = m_tracker->getAllSatellites();
    }

    QHash<int, Satellite> byCatalog;
    for (const Satellite& sat : satellites) byCatalog.insert(sat.catalogNumber, sat);

    QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonArray arr;
    for (const SatellitePass& pass : PassPredictor::predictAll(satellites, m_tracker->getObserverLocation(),
                                                               now, hours, minElevation)) {
        const Satellite& sat = byCatalog[pass.catalogNumber];
        int index = m_radio->defaultTransponder(sat);
        if (receivableOnly && index < 0) continue;

        QJsonObject obj{
            {"satellite", pass.satelliteName},
            {"catalogNumber", pass.catalogNumber},
            {"aosUtc", pass.aos.toString(Qt::ISODate)},
            {"tcaUtc", pass.tca.toString(Qt::ISODate)},
            {"losUtc", pass.los.toString(Qt::ISODate)},
            {"aosLocal", pass.aos.toLocalTime().toString(Qt::ISODate)},
            {"maxElevation", pass.maxElevation},
            {"aosAzimuth", pass.aosAzimuth},
            {"tcaAzimuth", pass.tcaAzimuth},
            {"losAzimuth", pass.losAzimuth},
            {"direction", PassPredictor::directionText(pass)},
            {"durationSeconds", pass.durationSeconds()},
            {"inProgress", pass.isInProgress(now)},
            {"aosBeforeSearch", pass.aosBeforeSearch},
            {"losAfterSearch", pass.losAfterSearch}
        };
        if (index >= 0) {
            const Transponder& t = sat.transponders[index];
            obj["downlink"] = QJsonObject{
                {"transponderIndex", index},
                {"name", t.name},
                {"downlinkMHz", t.downlinkFreq},
                {"mode", t.mode}
            };
        }
        arr.append(obj);
    }

    Response r;
    r.body = QJsonDocument(QJsonObject{
        {"timeUtc", now.toString(Qt::ISODate)},
        {"observer", observerToJson(m_tracker->getObserverLocation())},
        {"hours", hours},
        {"minElevation", minElevation},
        {"count", arr.size()},
        {"passes", arr}
    });
    return r;
}

QJsonObject ApiServer::radioStatusToJson(const RadioController::Status& s) {
    QJsonObject obj{
        {"connected", s.connected},
        {"tracking", s.tracking},
        {"host", s.host},
        {"port", s.port},
        {"message", s.message}
    };
    if (s.armed) {
        obj["armed"] = QJsonObject{
            {"satellite", s.armedSatelliteName},
            {"catalogNumber", s.armedCatalogNumber},
            {"transponderIndex", s.armedTransponderIndex},
            {"aosUtc", s.armedAosUtc.toString(Qt::ISODate)},
            {"tuneAtUtc", s.armedAosUtc.addSecs(-RadioController::kPreTuneSeconds).toString(Qt::ISODate)}
        };
    }
    if (s.tracking || s.tunedHz > 0) {
        obj["satellite"] = s.satelliteName;
        obj["catalogNumber"] = s.catalogNumber;
        obj["transponderIndex"] = s.transponderIndex;
        obj["transponder"] = QJsonObject{
            {"name", s.transponder.name},
            {"downlinkMHz", s.transponder.downlinkFreq},
            {"mode", s.transponder.mode}
        };
        obj["rigMode"] = s.rigMode;
        obj["elevation"] = s.elevation;
        obj["rangeRateKmPerSec"] = s.rangeRate;
        obj["nominalHz"] = double(s.nominalHz);
        obj["dopplerHz"] = double(s.dopplerHz);
        obj["manualOffsetHz"] = double(s.offsetHz);
        obj["tunedHz"] = double(s.tunedHz);
    }
    return obj;
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
