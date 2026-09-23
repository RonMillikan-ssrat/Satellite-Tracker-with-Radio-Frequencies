#ifndef SATELLITETRACKER_H
#define SATELLITETRACKER_H

#include <QObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QHash>
#include "satellite.h"

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
    
    // Fetch TLE data from CelesTrak
    void fetchTLEData(const QString& tleUrl = "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle");
    
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
    
private slots:
    void onTLEReplyFinished();
    
private:
    ObserverLocation m_observer;
    QList<Satellite> m_satellites;
    QDateTime m_positionsTime;
    QNetworkAccessManager* m_networkManager;
    
    QHash<int, QList<Transponder>> m_satnogsTransponders;  // keyed by NORAD catalog number
    
    void parseLocationData(const QByteArray& data);
    bool loadTransmitterData(const QByteArray& data);
    QString transmitterCachePath() const;
    void applyTransponders(Satellite& sat) const;
};

#endif // SATELLITETRACKER_H
