#include "sgp4wrapper.h"
#include <libsgp4/SGP4.h>
#include <libsgp4/Observer.h>
#include <libsgp4/CoordGeodetic.h>
#include <libsgp4/CoordTopocentric.h>
#include <libsgp4/Util.h>
#include <exception>

SGP4Wrapper::SGP4Wrapper() {}

SatellitePosition SGP4Wrapper::calculatePosition(const Satellite& sat,
                                                  const QDateTime& time,
                                                  const ObserverLocation& observer) {
    SatellitePosition pos{};
    pos.elevation = -90.0;

    try {
        libsgp4::Tle tle(sat.name.toStdString(),
                         sat.line1.toStdString(),
                         sat.line2.toStdString());
        libsgp4::SGP4 sgp4(tle);

        QDateTime utc = time.toUTC();
        QDate d = utc.date();
        QTime t = utc.time();
        libsgp4::DateTime dt(d.year(), d.month(), d.day(),
                             t.hour(), t.minute(), t.second(), t.msec() * 1000);

        libsgp4::Eci eci = sgp4.FindPosition(dt);

        libsgp4::CoordGeodetic geo = eci.ToGeodetic();
        pos.latitude = libsgp4::Util::RadiansToDegrees(geo.latitude);
        pos.longitude = libsgp4::Util::RadiansToDegrees(geo.longitude);
        pos.altitude = geo.altitude;

        // Observer altitude is stored in meters; libsgp4 expects kilometers
        libsgp4::Observer obs(observer.latitude, observer.longitude,
                              observer.altitude / 1000.0);
        libsgp4::CoordTopocentric topo = obs.GetLookAngle(eci);
        pos.azimuth = libsgp4::Util::RadiansToDegrees(topo.azimuth);
        pos.elevation = libsgp4::Util::RadiansToDegrees(topo.elevation);
        pos.range = topo.range;
        pos.rangerate = topo.range_rate;
    } catch (const std::exception&) {
        // Invalid TLE or decayed satellite: leave as not visible
    }

    return pos;
}
