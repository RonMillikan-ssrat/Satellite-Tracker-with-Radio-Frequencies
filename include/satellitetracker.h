#ifndef SATELLITETRACKER_H
#define SATELLITETRACKER_H

#include <QObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include "satellite.h"
#include "catalog.h"

class SatelliteTracker : public QObject {
    Q_OBJECT
    
public:
    explicit SatelliteTracker(QObject *parent = nullptr);
    ~SatelliteTracker();
    
    // Set observer location
    void setObserverLocation(const ObserverLocation& location);
    ObserverLocation getObserverLocation() const { return m_observer; }
    
    // Get current location via IP geolocation
    void fetchCurrentLocation();
    
    // Fallback geolocation services
    void fetchLocationFromIpApi();
    void fetchLocationFromIpInfo();
    void fetchLocationFromIpApiCom();
    
    // CelesTrak refreshes GP data about every 2 hours and blocks clients that
    // download the same data more often, so a catalog is never re-downloaded
    // while its cached copy is younger than this.
    static constexpr int kMinRefreshSeconds = 2 * 3600;

    // Load a catalog (see catalog.h). Nothing is downloaded unless a catalog is
    // requested: a cached copy younger than kMinRefreshSeconds is loaded from
    // disk, otherwise the catalog is fetched from CelesTrak (falling back to the
    // stale cache if the download fails). force = the user asked for a refresh;
    // it still respects kMinRefreshSeconds. Returns false for an unknown id.
    bool loadCatalog(const QString& catalogId, bool force = false);

    struct CatalogState {
        QString id;              // catalog id, or "custom" after fetchTLEData(url)
        QString label;
        int satelliteCount = 0;
        QDateTime dataTimeUtc;   // when the loaded data was downloaded from CelesTrak
        bool fromCache = false;  // loaded from the on-disk cache rather than a fresh download
        bool loading = false;    // a download is in progress
        QString loadingId;       // catalog being downloaded
        QString lastError;       // why the last catalog download failed (empty after a success)
    };
    CatalogState catalogState() const;

    // Fetch TLE data from an arbitrary URL (bypasses the catalog cache)
    void fetchTLEData(const QString& tleUrl);
    
    // Fetch radio transmitter data from the SatNOGS DB (cached on disk)
    void fetchTransmitterData(const QString& url = "https://db.satnogs.org/api/transmitters/?format=json");
    
    // Number of satellites with SatNOGS transmitter data loaded
    int transmitterSatelliteCount() const { return m_satnogsTransponders.size(); }
    
    // Update satellite positions
    void updatePositions();
    
    // Get visible satellites
    QList<Satellite> getVisibleSatellites() const;
    
    // UTC time the current positions were calculated for
    QDateTime getPositionsTime() const { return m_positionsTime; }
    
    // Get all satellites
    QList<Satellite> getAllSatellites() const { return m_satellites; }
    
    // Built-in transponder table for well-known satellites (fallback when SatNOGS has no entry)
    static void populateTransponders(Satellite& sat);
    
    // Calculate ground track for a satellite (future positions)
    static QList<QPointF> calculateGroundTrack(const Satellite& sat,
                                                const ObserverLocation& observer,
                                                int minutes = 20,
                                                int stepSeconds = 30);
    
signals:
    void locationUpdated(const ObserverLocation& location);
    void tleDataUpdated(int satelliteCount);
    void positionsUpdated();
    void errorOccurred(const QString& error);
    void statusMessage(const QString& message);
    void catalogLoading(const QString& catalogId);   // download started
    void catalogLoaded(const QString& catalogId);    // satellites replaced (after tleDataUpdated)
    
private:
    ObserverLocation m_observer;
    QList<Satellite> m_satellites;
    QDateTime m_positionsTime;
    QNetworkAccessManager* m_networkManager;
    
    QHash<int, QList<Transponder>> m_satnogsTransponders;  // keyed by NORAD catalog number
    
    // Catalog state
    QString m_catalogId;
    QString m_catalogLabel;
    QDateTime m_catalogDataTime;
    bool m_catalogFromCache = false;
    QString m_catalogError;

    // In-flight catalog download. Replies from an older generation (the user
    // switched catalogs mid-download) are ignored.
    struct PendingDownload {
        int generation = 0;
        QString catalogId;
        int outstanding = 0;
        QStringList tleParts;
        QByteArray satcat;
        QStringList errors;      // fatal: the SATCAT owner filter could not be applied
        QStringList gpFailures;  // GP queries that failed; fatal only if every query failed
    };
    PendingDownload m_pending;
    int m_catalogGeneration = 0;

    QString catalogCachePath(const QString& catalogId) const;
    bool loadCatalogFromCache(const SatelliteCatalog& catalog);
    void downloadCatalog(const SatelliteCatalog& catalog);
    void requestSatcat(const QString& url, int generation, bool isFallback);
    void finishCatalogDownload();
    void maybeRefreshTransmitters(int maxAgeSeconds);
    bool m_transmitterFetchInFlight = false;
    void setSatellites(const QList<Satellite>& satellites);
    static QSet<int> parseSatcatOwners(const QByteArray& csv, const QStringList& owners, bool* ok);
    static QString toTLEText(const QList<Satellite>& satellites);

    void parseLocationData(const QByteArray& data);
    bool loadTransmitterData(const QByteArray& data);
    QString transmitterCachePath() const;
    void applyTransponders(Satellite& sat) const;
};

#endif // SATELLITETRACKER_H
