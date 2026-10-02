#include "satellitetracker.h"
#include "tleparser.h"
#include "sgp4wrapper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

SatelliteTracker::SatelliteTracker(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Default location (will be updated)
    m_observer = ObserverLocation(39.7392, -104.9903, 1609.0); // Denver, CO
    
    // Set network timeout
    m_networkManager->setTransferTimeout(10000); // 10 seconds
    
    // Load cached SatNOGS transmitter data so frequencies are available offline
    QFile cache(transmitterCachePath());
    if (cache.open(QIODevice::ReadOnly)) {
        loadTransmitterData(cache.readAll());
    }
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
    // An explicit URL replaces whatever catalog is loaded or loading
    m_pending = PendingDownload();
    int generation = ++m_catalogGeneration;

    QNetworkRequest request{QUrl(tleUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
    request.setTransferTimeout(60000);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, tleUrl]() {
        reply->deleteLater();
        if (generation != m_catalogGeneration) return;  // superseded by a catalog load
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch TLE data: " + reply->errorString());
            return;
        }
        QList<Satellite> satellites = TLEParser::parseTLEData(QString::fromUtf8(reply->readAll()));
        if (satellites.isEmpty()) {
            emit errorOccurred("No satellites found at " + tleUrl);
            return;
        }
        m_catalogId = "custom";
        m_catalogLabel = "Custom URL";
        m_catalogDataTime = QDateTime::currentDateTimeUtc();
        m_catalogFromCache = false;
        m_catalogError.clear();
        setSatellites(satellites);
        emit catalogLoaded(m_catalogId);
    });

    // Refresh radio frequencies alongside the orbital elements
    maybeRefreshTransmitters(kMinRefreshSeconds);
}

bool SatelliteTracker::loadCatalog(const QString& catalogId, bool force) {
    const SatelliteCatalog* catalog = SatelliteCatalog::find(catalogId);
    if (!catalog) {
        emit errorOccurred("Unknown catalog: " + catalogId);
        return false;
    }

    // Already downloading this one: let that finish
    if (m_pending.outstanding > 0 && m_pending.catalogId == catalog->id) {
        return true;
    }

    QFileInfo cache(catalogCachePath(catalog->id));
    qint64 ageSeconds = cache.exists()
        ? cache.lastModified().toUTC().secsTo(QDateTime::currentDateTimeUtc()) : -1;
    bool fresh = ageSeconds >= 0 && ageSeconds < kMinRefreshSeconds;

    if (fresh && loadCatalogFromCache(*catalog)) {
        if (force) {
            emit statusMessage(QString("%1 data is %2 min old. CelesTrak updates about every "
                                       "2 hours, so it was reloaded from the cache.")
                                   .arg(catalog->label)
                                   .arg(ageSeconds / 60));
        }
        maybeRefreshTransmitters(24 * 3600);
        return true;
    }

    downloadCatalog(*catalog);
    maybeRefreshTransmitters(force ? kMinRefreshSeconds : 24 * 3600);
    return true;
}

SatelliteTracker::CatalogState SatelliteTracker::catalogState() const {
    CatalogState state;
    state.id = m_catalogId;
    state.label = m_catalogLabel;
    state.satelliteCount = m_satellites.size();
    state.dataTimeUtc = m_catalogDataTime;
    state.fromCache = m_catalogFromCache;
    state.loading = m_pending.outstanding > 0;
    state.loadingId = state.loading ? m_pending.catalogId : QString();
    state.lastError = m_catalogError;
    return state;
}

QString SatelliteTracker::catalogCachePath(const QString& catalogId) const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + "/catalogs/" + catalogId + ".tle";
}

bool SatelliteTracker::loadCatalogFromCache(const SatelliteCatalog& catalog) {
    QString path = catalogCachePath(catalog.id);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QList<Satellite> satellites = TLEParser::parseTLEData(QString::fromUtf8(file.readAll()));
    if (satellites.isEmpty()) return false;

    // Cancel any download in flight; the cache wins
    m_pending = PendingDownload();
    ++m_catalogGeneration;

    m_catalogId = catalog.id;
    m_catalogLabel = catalog.label;
    m_catalogDataTime = QFileInfo(path).lastModified().toUTC();
    m_catalogFromCache = true;
    m_catalogError.clear();
    setSatellites(satellites);
    emit catalogLoaded(catalog.id);
    return true;
}

void SatelliteTracker::downloadCatalog(const SatelliteCatalog& catalog) {
    m_pending = PendingDownload();
    m_pending.generation = ++m_catalogGeneration;
    m_pending.catalogId = catalog.id;
    const int generation = m_pending.generation;

    emit catalogLoading(catalog.id);
    emit statusMessage(QString("Downloading %1 from CelesTrak...").arg(catalog.label));

    for (const QString& query : catalog.gpQueries) {
        ++m_pending.outstanding;
        QNetworkRequest request{QUrl(SatelliteCatalog::gpUrl(query))};
        request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
        request.setTransferTimeout(60000);  // GROUP=active is a few MB
        QNetworkReply* reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation, query]() {
            reply->deleteLater();
            if (generation != m_catalogGeneration) return;
            if (reply->error() == QNetworkReply::NoError) {
                m_pending.tleParts.append(QString::fromUtf8(reply->readAll()));
            } else {
                int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                m_pending.gpFailures.append(QString("%1 (%2)")
                                                .arg(query, http > 0 ? QString("HTTP %1").arg(http)
                                                                     : reply->errorString()));
            }
            if (--m_pending.outstanding == 0) finishCatalogDownload();
        });
    }

    if (!catalog.owners.isEmpty()) {
        ++m_pending.outstanding;
        requestSatcat(SatelliteCatalog::satcatUrl(), generation, false);
    }
}

void SatelliteTracker::requestSatcat(const QString& url, int generation, bool isFallback) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
    request.setTransferTimeout(90000);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, isFallback, url]() {
        reply->deleteLater();
        if (generation != m_catalogGeneration) return;

        QByteArray data;
        QString problem;
        if (reply->error() == QNetworkReply::NoError) {
            data = reply->readAll();
            if (!data.contains("NORAD_CAT_ID")) problem = "response is not SATCAT CSV";
        } else {
            problem = reply->errorString();
        }

        if (problem.isEmpty()) {
            m_pending.satcat = data;
        } else if (!isFallback) {
            requestSatcat(SatelliteCatalog::satcatFallbackUrl(), generation, true);
            return;  // still outstanding
        } else {
            m_pending.errors.append("SATCAT (" + url + "): " + problem);
        }
        if (--m_pending.outstanding == 0) finishCatalogDownload();
    });
}

namespace {
// Split one CSV line, honouring double-quoted fields ("a,b" and "" escapes)
QList<QByteArray> splitCsvLine(const QByteArray& line) {
    QList<QByteArray> fields;
    QByteArray field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.append(field);
            field.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    fields.append(field);
    return fields;
}

bool isDebrisName(const QString& name) {
    return name.endsWith(" R/B") || name.contains(" DEB");
}
}

QSet<int> SatelliteTracker::parseSatcatOwners(const QByteArray& csv, const QStringList& owners, bool* ok) {
    QSet<int> catalogNumbers;
    *ok = false;
    QList<QByteArray> lines = csv.split('\n');
    if (lines.isEmpty()) return catalogNumbers;

    QList<QByteArray> header = splitCsvLine(lines.first().trimmed());
    int noradColumn = header.indexOf("NORAD_CAT_ID");
    int ownerColumn = header.indexOf("OWNER");
    if (noradColumn < 0 || ownerColumn < 0) return catalogNumbers;

    QSet<QByteArray> wanted;
    for (const QString& owner : owners) wanted.insert(owner.toUtf8());

    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        QList<QByteArray> fields = splitCsvLine(lines[i]);
        if (fields.size() <= qMax(noradColumn, ownerColumn)) continue;
        if (wanted.contains(fields[ownerColumn].trimmed())) {
            catalogNumbers.insert(fields[noradColumn].trimmed().toInt());
        }
    }
    *ok = true;
    return catalogNumbers;
}

QString SatelliteTracker::toTLEText(const QList<Satellite>& satellites) {
    QString text;
    for (const Satellite& sat : satellites) {
        text += sat.name + '\n' + sat.line1 + '\n' + sat.line2 + '\n';
    }
    return text;
}

void SatelliteTracker::finishCatalogDownload() {
    PendingDownload done = m_pending;
    m_pending = PendingDownload();
    const SatelliteCatalog* catalog = SatelliteCatalog::find(done.catalogId);
    if (!catalog) return;

    QSet<int> ownedBy;
    bool filterByOwner = !catalog->owners.isEmpty();
    if (filterByOwner && done.errors.isEmpty()) {
        bool ok = false;
        ownedBy = parseSatcatOwners(done.satcat, catalog->owners, &ok);
        if (!ok) done.errors.append("SATCAT CSV has no NORAD_CAT_ID/OWNER columns");
    }

    // Merge the parts: drop duplicates (name queries can overlap), rocket
    // bodies and debris, and anything outside the owner filter
    QList<Satellite> merged;
    if (done.errors.isEmpty()) {
        QSet<int> seen;
        for (const QString& part : done.tleParts) {
            for (const Satellite& sat : TLEParser::parseTLEData(part)) {
                if (seen.contains(sat.catalogNumber) || isDebrisName(sat.name)) continue;
                if (filterByOwner && !ownedBy.contains(sat.catalogNumber)) continue;
                seen.insert(sat.catalogNumber);
                merged.append(sat);
            }
        }
    }

    // Every GP query failed: treat as a failed download. If only some failed
    // (e.g. one name in a multi-name catalog), keep what did arrive.
    if (done.tleParts.isEmpty() && !done.gpFailures.isEmpty()) {
        done.errors.append(done.gpFailures);
    }

    if (!done.errors.isEmpty() || merged.isEmpty()) {
        QString why = done.errors.isEmpty() ? QString("no satellites returned") : done.errors.join("; ");
        QString message;
        if (loadCatalogFromCache(*catalog)) {
            message = QString("Could not download %1 (%2). Using cached data from %3.")
                          .arg(catalog->label, why,
                               m_catalogDataTime.toLocalTime().toString("ddd h:mm AP"));
        } else {
            message = QString("Could not download %1: %2. %3 is still loaded.")
                          .arg(catalog->label, why, m_catalogLabel.isEmpty() ? "Nothing" : m_catalogLabel);
        }
        m_catalogError = message;
        emit errorOccurred(message);
        return;
    }

    QString path = catalogCachePath(catalog->id);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile cache(path);
    if (cache.open(QIODevice::WriteOnly)) {
        cache.write(toTLEText(merged).toUtf8());
        cache.commit();
    }

    m_catalogId = catalog->id;
    m_catalogLabel = catalog->label;
    m_catalogDataTime = QDateTime::currentDateTimeUtc();
    m_catalogFromCache = false;
    m_catalogError = done.gpFailures.isEmpty()
        ? QString()
        : QString("Loaded %1 without: %2").arg(catalog->label, done.gpFailures.join(", "));
    setSatellites(merged);
    emit catalogLoaded(catalog->id);
    if (!m_catalogError.isEmpty()) emit statusMessage(m_catalogError);
}

void SatelliteTracker::maybeRefreshTransmitters(int maxAgeSeconds) {
    if (m_transmitterFetchInFlight) return;
    QFileInfo cache(transmitterCachePath());
    bool stale = !cache.exists() || m_satnogsTransponders.isEmpty()
                 || cache.lastModified().toUTC().secsTo(QDateTime::currentDateTimeUtc()) > maxAgeSeconds;
    if (stale) fetchTransmitterData();
}

void SatelliteTracker::setSatellites(const QList<Satellite>& satellites) {
    m_satellites = satellites;
    // Populate transponder frequencies (SatNOGS, else built-in table)
    for (Satellite& sat : m_satellites) {
        applyTransponders(sat);
    }
    emit tleDataUpdated(m_satellites.size());
    updatePositions();
}

void SatelliteTracker::fetchTransmitterData(const QString& url) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "SatelliteTracker/1.0");
    request.setTransferTimeout(60000); // ~2 MB download, allow more than the default
    QNetworkReply* reply = m_networkManager->get(request);
    m_transmitterFetchInFlight = true;
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_transmitterFetchInFlight = false;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (loadTransmitterData(data)) {
                QDir().mkpath(QFileInfo(transmitterCachePath()).absolutePath());
                QSaveFile cache(transmitterCachePath());
                if (cache.open(QIODevice::WriteOnly)) {
                    cache.write(data);
                    cache.commit();
                }
                for (Satellite& sat : m_satellites) {
                    applyTransponders(sat);
                }
                emit positionsUpdated();
            } else {
                emit errorOccurred("Invalid transmitter data from SatNOGS");
            }
        } else {
            emit errorOccurred("Failed to fetch SatNOGS transmitter data: " + reply->errorString());
        }
        reply->deleteLater();
    });
}

bool SatelliteTracker::loadTransmitterData(const QByteArray& data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return false;
    
    QHash<int, QList<Transponder>> byCatalog;
    for (const QJsonValue& v : doc.array()) {
        QJsonObject t = v.toObject();
        if (!t["alive"].toBool() || t["status"].toString() != "active") continue;
        
        int catalogNumber = t["norad_cat_id"].toInt();
        // SatNOGS frequencies are in Hz; null means none
        double uplink = t["uplink_low"].toDouble() / 1e6;
        double downlink = t["downlink_low"].toDouble() / 1e6;
        if (catalogNumber <= 0 || (uplink <= 0 && downlink <= 0)) continue;
        
        byCatalog[catalogNumber].append(Transponder(t["description"].toString(),
                                                    uplink, downlink,
                                                    t["mode"].toString(),
                                                    t["invert"].toBool()));
    }
    
    m_satnogsTransponders = byCatalog;
    return true;
}

QString SatelliteTracker::transmitterCachePath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/satnogs_transmitters.json";
}

void SatelliteTracker::applyTransponders(Satellite& sat) const {
    auto it = m_satnogsTransponders.constFind(sat.catalogNumber);
    if (it != m_satnogsTransponders.constEnd()) {
        sat.transponders = it.value();
    } else {
        sat.transponders.clear();
        populateTransponders(sat);
    }
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
