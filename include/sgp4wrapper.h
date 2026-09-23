#ifndef SGP4WRAPPER_H
#define SGP4WRAPPER_H

#include "satellite.h"
#include <QDateTime>

// Orbital propagation via the full SGP4/SDP4 model (libsgp4 by dnwrnr,
// vendored in external/sgp4).
class SGP4Wrapper {
public:
    SGP4Wrapper();

    // Calculate satellite position at given time from observer location.
    // If the TLE is invalid or the satellite has decayed, the returned
    // position has elevation -90 so it is never treated as visible.
    static SatellitePosition calculatePosition(const Satellite& sat,
                                               const QDateTime& time,
                                               const ObserverLocation& observer);
};

#endif // SGP4WRAPPER_H
