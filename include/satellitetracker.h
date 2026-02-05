#ifndef SATELLITETRACKER_H
#define SATELLITETRACKER_H

#include <QObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
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
    
    // Update satellite positions
    void updatePositions();
    
    // Get visible satellites
    QList<Satellite> getVisibleSatellites() const;
    
    // Get all satellites
    QList<Satellite> getAllSatellites() const { return m_satellites; }
    
    // Populate transponder frequencies for known satellites
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
    QNetworkAccessManager* m_networkManager;
    
    void parseLocationData(const QByteArray& data);
};

#endif // SATELLITETRACKER_H
