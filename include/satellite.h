#ifndef SATELLITE_H
#define SATELLITE_H

#include <QString>
#include <QDateTime>
#include <QList>
#include <QPointF>

struct Transponder {
    QString name;           // "FM Voice", "Linear U/V", etc.
    double uplinkFreq;      // MHz
    double downlinkFreq;    // MHz
    QString mode;           // "FM", "SSB", "CW", "Digital", etc.
    bool inverting;         // true for inverting transponders
    
    Transponder() : uplinkFreq(0.0), downlinkFreq(0.0), inverting(false) {}
    Transponder(const QString& n, double up, double down, const QString& m, bool inv = false)
        : name(n), uplinkFreq(up), downlinkFreq(down), mode(m), inverting(inv) {}
};

struct SatellitePosition {
    double latitude;      // degrees
    double longitude;     // degrees
    double altitude;      // km
    double azimuth;       // degrees from observer
    double elevation;     // degrees from observer
    double range;         // km from observer
    double rangerate;     // km/s (negative = approaching, positive = departing)
};

struct Satellite {
    QString name;
    QString line1;        // TLE line 1
    QString line2;        // TLE line 2
    int catalogNumber;
    SatellitePosition position;
    bool isVisible;       // Above horizon
    QList<Transponder> transponders;  // Radio frequencies
    QList<QPointF> groundTrack;       // Az/El points for track visualization
    
    Satellite() : catalogNumber(0), isVisible(false) {}
};

struct ObserverLocation {
    double latitude;      // degrees
    double longitude;     // degrees
    double altitude;      // meters
    
    ObserverLocation() : latitude(0.0), longitude(0.0), altitude(0.0) {}
    ObserverLocation(double lat, double lon, double alt = 0.0) 
        : latitude(lat), longitude(lon), altitude(alt) {}
};

#endif // SATELLITE_H
