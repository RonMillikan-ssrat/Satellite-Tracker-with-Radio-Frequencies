#include "satellitetracker.h"
#include "tleparser.h"
#include "sgp4wrapper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

SatelliteTracker::SatelliteTracker(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Default location (will be updated)
    m_observer = ObserverLocation(39.7392, -104.9903, 1609.0); // Denver, CO
    
    // Set network timeout
    m_networkManager->setTransferTimeout(10000); // 10 seconds
}

SatelliteTracker::~SatelliteTracker() {
}

void SatelliteTracker::setObserverLocation(const ObserverLocation& location) {
    m_observer = location;
    emit locationUpdated(m_observer);
    updatePositions();
}

void SatelliteTracker::fetchCurrentLocation() {
    // Try ipapi.co first
    fetchLocationFromIpApiCom();
}

void SatelliteTracker::fetchLocationFromIpApiCom() {
    QNetworkRequest request{QUrl("https://ipapi.co/json/")};
    request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            parseLocationData(data);
        } else {
            // Try ipapi.com as fallback
            emit errorOccurred("ipapi.co failed, trying ipapi.com...");
            fetchLocationFromIpApi();
        }
        reply->deleteLater();
    });
}

void SatelliteTracker::fetchLocationFromIpApi() {
    QNetworkRequest request{QUrl("http://ip-api.com/json/")};
    request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                emit errorOccurred("Invalid location data from ip-api.com");
                fetchLocationFromIpInfo();
                reply->deleteLater();
                return;
            }
            
            QJsonObject obj = doc.object();
            if (obj.contains("lat") && obj.contains("lon")) {
                double lat = obj["lat"].toDouble();
                double lon = obj["lon"].toDouble();
                m_observer = ObserverLocation(lat, lon, 0.0);
                emit locationUpdated(m_observer);
                updatePositions();
            } else {
                emit errorOccurred("ip-api.com missing coordinates, trying ipinfo.io...");
                fetchLocationFromIpInfo();
            }
        } else {
            emit errorOccurred("ip-api.com failed, trying ipinfo.io...");
            fetchLocationFromIpInfo();
        }
        reply->deleteLater();
    });
}

void SatelliteTracker::fetchLocationFromIpInfo() {
    QNetworkRequest request{QUrl("https://ipinfo.io/json")};
    request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) {
                emit errorOccurred("All geolocation services failed. Please enter location manually.");
                reply->deleteLater();
                return;
            }
            
            QJsonObject obj = doc.object();
            if (obj.contains("loc")) {
                QString loc = obj["loc"].toString();
                QStringList coords = loc.split(',');
                if (coords.size() == 2) {
                    bool ok1, ok2;
                    double lat = coords[0].toDouble(&ok1);
                    double lon = coords[1].toDouble(&ok2);
                    if (ok1 && ok2) {
                        m_observer = ObserverLocation(lat, lon, 0.0);
                        emit locationUpdated(m_observer);
                        updatePositions();
                    } else {
                        emit errorOccurred("All geolocation services failed. Please enter location manually.");
                    }
                } else {
                    emit errorOccurred("All geolocation services failed. Please enter location manually.");
                }
            } else {
                emit errorOccurred("All geolocation services failed. Please enter location manually.");
            }
        } else {
            emit errorOccurred("All geolocation services failed. Please enter location manually.");
        }
        reply->deleteLater();
    });
}

void SatelliteTracker::parseLocationData(const QByteArray& data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit errorOccurred("Invalid location data received");
        return;
    }
    
    QJsonObject obj = doc.object();
    
    if (obj.contains("latitude") && obj.contains("longitude")) {
        double lat = obj["latitude"].toDouble();
        double lon = obj["longitude"].toDouble();
        
        // Assume sea level if not provided
        m_observer = ObserverLocation(lat, lon, 0.0);
        
        emit locationUpdated(m_observer);
        updatePositions();
    } else {
        emit errorOccurred("Location data missing coordinates");
    }
}

void SatelliteTracker::fetchTLEData(const QString& tleUrl) {
    QNetworkRequest request{QUrl(tleUrl)};
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &SatelliteTracker::onTLEReplyFinished);
}

void SatelliteTracker::onTLEReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        QString tleData = QString::fromUtf8(reply->readAll());
        m_satellites = TLEParser::parseTLEData(tleData);
        
        // Populate transponder frequencies for known satellites
        for (Satellite& sat : m_satellites) {
            populateTransponders(sat);
        }
        
        emit tleDataUpdated(m_satellites.size());
        updatePositions();
    } else {
        emit errorOccurred("Failed to fetch TLE data: " + reply->errorString());
    }
    
    reply->deleteLater();
}

void SatelliteTracker::updatePositions() {
    QDateTime now = QDateTime::currentDateTimeUtc();
    m_positionsTime = now;
    
    for (int i = 0; i < m_satellites.size(); ++i) {
        Satellite& sat = m_satellites[i];
        
        // Calculate current position
        sat.position = SGP4Wrapper::calculatePosition(sat, now, m_observer);
        
        // Determine if visible (above horizon)
        sat.isVisible = (sat.position.elevation > 0.0);
        
        // Calculate ground track (only for visible satellites to save CPU)
        if (sat.isVisible) {
            sat.groundTrack = calculateGroundTrack(sat, m_observer, 20, 30);
        } else {
            sat.groundTrack.clear();
        }
    }
    
    emit positionsUpdated();
}

QList<Satellite> SatelliteTracker::getVisibleSatellites() const {
    QList<Satellite> visible;
    
    for (const Satellite& sat : m_satellites) {
        if (sat.isVisible) {
            visible.append(sat);
        }
    }
    
    return visible;
}

void SatelliteTracker::populateTransponders(Satellite& sat) {
    // Amateur radio satellite frequency database
    // Frequencies in MHz
    
    QString satName = sat.name.toUpper();
    
    // ISS
    if (satName.contains("ISS") || satName.contains("ZARYA")) {
        sat.transponders.append(Transponder("APRS", 145.825, 145.825, "FM/APRS"));
        sat.transponders.append(Transponder("Voice Repeater", 145.990, 437.800, "FM"));
        sat.transponders.append(Transponder("SSTV", 145.800, 145.800, "FM/SSTV"));
    }
    // AO-91 (Fox-1B)
    else if (satName.contains("AO-91") || satName.contains("FOX-1B")) {
        sat.transponders.append(Transponder("FM Voice", 435.250, 145.960, "FM"));
        sat.transponders.append(Transponder("Telemetry", 0.0, 145.960, "DUV"));
    }
    // AO-92 (Fox-1D)
    else if (satName.contains("AO-92") || satName.contains("FOX-1D")) {
        sat.transponders.append(Transponder("FM Voice", 435.350, 145.880, "FM"));
        sat.transponders.append(Transponder("Telemetry", 0.0, 145.880, "DUV"));
    }
    // SO-50 (SaudiSat 1C)
    else if (satName.contains("SO-50") || satName.contains("SAUDISAT")) {
        sat.transponders.append(Transponder("FM Voice", 145.850, 436.795, "FM"));
        sat.transponders.append(Transponder("Tone", 74.4, 0.0, "CTCSS")); // 74.4 Hz CTCSS
    }
    // AO-7
    else if (satName.contains("AO-7") || satName.contains("AMSAT-OSCAR 7")) {
        sat.transponders.append(Transponder("Mode A", 145.850, 29.400, "CW/SSB", true));
        sat.transponders.append(Transponder("Mode B", 432.125, 145.975, "CW/SSB", true));
    }
    // FO-29
    else if (satName.contains("FO-29") || satName.contains("FUJI-OSCAR 29")) {
        sat.transponders.append(Transponder("Mode JA", 145.900, 435.900, "CW/SSB", true));
        sat.transponders.append(Transponder("Mode JD", 145.900, 435.900, "Digital"));
    }
    // AO-73 (FUNcube-1)
    else if (satName.contains("AO-73") || satName.contains("FUNCUBE")) {
        sat.transponders.append(Transponder("Linear U/V", 435.150, 145.935, "SSB/CW", true));
        sat.transponders.append(Transponder("Telemetry", 0.0, 145.935, "BPSK"));
    }
    // CAS-4A
    else if (satName.contains("CAS-4A")) {
        sat.transponders.append(Transponder("Linear U/V", 435.210, 145.870, "CW/SSB", true));
        sat.transponders.append(Transponder("Telemetry", 0.0, 145.870, "BPSK"));
    }
    // CAS-4B
    else if (satName.contains("CAS-4B")) {
        sat.transponders.append(Transponder("Linear U/V", 435.240, 145.895, "CW/SSB", true));
        sat.transponders.append(Transponder("Telemetry", 0.0, 145.895, "BPSK"));
    }
    // XW-2A through XW-2F (CAS-3 constellation)
    else if (satName.contains("XW-2A")) {
        sat.transponders.append(Transponder("Linear U/V", 435.030, 145.660, "CW/SSB", true));
    }
    else if (satName.contains("XW-2B")) {
        sat.transponders.append(Transponder("Linear U/V", 435.180, 145.780, "CW/SSB", true));
    }
    else if (satName.contains("XW-2C")) {
        sat.transponders.append(Transponder("Linear U/V", 435.060, 145.790, "CW/SSB", true));
    }
    else if (satName.contains("XW-2D")) {
        sat.transponders.append(Transponder("Linear U/V", 435.220, 145.870, "CW/SSB", true));
    }
    else if (satName.contains("XW-2F")) {
        sat.transponders.append(Transponder("Linear U/V", 435.080, 145.910, "CW/SSB", true));
    }
    // IO-86 (LapanA2)
    else if (satName.contains("IO-86") || satName.contains("LAPAN")) {
        sat.transponders.append(Transponder("FM Voice", 145.880, 437.050, "FM"));
    }
    // NO-84 (PSAT)
    else if (satName.contains("NO-84") || satName.contains("PSAT")) {
        sat.transponders.append(Transponder("FM Voice", 145.980, 435.350, "FM"));
    }
    // PO-101 (Diwata-2B)
    else if (satName.contains("PO-101") || satName.contains("DIWATA")) {
        sat.transponders.append(Transponder("FM Voice", 145.900, 437.500, "FM"));
    }
    // QO-100 (Es'hail-2) - Geostationary
    else if (satName.contains("QO-100") || satName.contains("ESHAIL")) {
        sat.transponders.append(Transponder("Narrowband", 2400.050, 10489.550, "SSB/CW", true));
        sat.transponders.append(Transponder("Wideband", 2401.500, 10491.500, "DVB-S2"));
    }
    // NOAA Weather Satellites
    else if (satName.contains("NOAA 15")) {
        sat.transponders.append(Transponder("APT", 0.0, 137.620, "APT"));
    }
    else if (satName.contains("NOAA 18")) {
        sat.transponders.append(Transponder("APT", 0.0, 137.9125, "APT"));
    }
    else if (satName.contains("NOAA 19")) {
        sat.transponders.append(Transponder("APT", 0.0, 137.100, "APT"));
    }
    // METEOR-M
    else if (satName.contains("METEOR-M")) {
        sat.transponders.append(Transponder("LRPT", 0.0, 137.100, "LRPT"));
        sat.transponders.append(Transponder("HRPT", 0.0, 1700.0, "HRPT"));
    }
}

QList<QPointF> SatelliteTracker::calculateGroundTrack(const Satellite& sat,
                                                       const ObserverLocation& observer,
                                                       int minutes,
                                                       int stepSeconds) {
    QList<QPointF> track;
    QDateTime now = QDateTime::currentDateTimeUtc();
    
    // Calculate future positions
    int totalSteps = (minutes * 60) / stepSeconds;
    
    for (int i = 0; i < totalSteps; ++i) {
        QDateTime futureTime = now.addSecs(i * stepSeconds);
        SatellitePosition futurePos = SGP4Wrapper::calculatePosition(sat, futureTime, observer);
        
        // Only include points above horizon
        if (futurePos.elevation > 0.0) {
            track.append(QPointF(futurePos.azimuth, futurePos.elevation));
        }
    }
    
    return track;
}
