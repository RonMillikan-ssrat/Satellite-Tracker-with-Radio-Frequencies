#ifndef PASSPREDICTOR_H
#define PASSPREDICTOR_H

#include <QDateTime>
#include <QList>
#include <QPointF>
#include "satellite.h"

// One pass of a satellite over the observer. Times are UTC.
struct SatellitePass {
    QString satelliteName;
    int catalogNumber = 0;
    QDateTime aos;              // rise (acquisition of signal)
    QDateTime tca;              // time of closest approach / maximum elevation
    QDateTime los;              // set (loss of signal)
    double aosAzimuth = 0.0;    // degrees
    double tcaAzimuth = 0.0;
    double losAzimuth = 0.0;
    double maxElevation = 0.0;
    bool aosBeforeSearch = false;  // already up when the search started (AOS is approximate)
    bool losAfterSearch = false;   // still up at the end of the search window

    int durationSeconds() const { return int(aos.secsTo(los)); }
    bool isInProgress(const QDateTime& nowUtc) const { return aos <= nowUtc && nowUtc < los; }
};

// Rise/set prediction using the full SGP4/SDP4 model (libsgp4).
//
// Elevation is sampled every 20 s; rise and set are then refined to the
// second by bisection and maximum elevation by golden-section search. A pass
// already in progress at the start time is traced back to its actual rise.
class PassPredictor {
public:
    // Passes of one satellite that are above the horizon at some point in
    // [startUtc, startUtc + hours] and reach at least minElevation degrees.
    static QList<SatellitePass> predictPasses(const Satellite& sat,
                                              const ObserverLocation& observer,
                                              const QDateTime& startUtc,
                                              double hours,
                                              double minElevation);

    // Passes of every satellite, sorted by rise time.
    static QList<SatellitePass> predictAll(const QList<Satellite>& satellites,
                                           const ObserverLocation& observer,
                                           const QDateTime& startUtc,
                                           double hours,
                                           double minElevation);

    // Az/el points (x = azimuth, y = elevation) along a pass for drawing.
    static QList<QPointF> passTrack(const Satellite& sat,
                                    const ObserverLocation& observer,
                                    const SatellitePass& pass,
                                    int stepSeconds = 10);

    // 16-point compass name for an azimuth, e.g. 300 -> "WNW".
    static QString compassPoint(double azimuth);

    // "NW → NE → SE" (rise, peak, set).
    static QString directionText(const SatellitePass& pass);

    // Compact duration: "45s", "10m 12s", "1h 23m", "2d 3h".
    static QString formatDuration(qint64 seconds);
};

#endif // PASSPREDICTOR_H
