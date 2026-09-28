#include "passpredictor.h"
#include <libsgp4/SGP4.h>
#include <libsgp4/Observer.h>
#include <libsgp4/CoordTopocentric.h>
#include <libsgp4/Util.h>
#include <algorithm>
#include <cmath>
#include <exception>

namespace {
const double kStepSeconds = 20.0;          // coarse sampling interval
const double kPrecisionSeconds = 1.0;      // rise/set/TCA refinement target
const double kMaxPassSeconds = 2 * 3600.0; // how far to trace a pass past the window edges

// Look angles for one satellite, with time as seconds from a fixed start.
class LookAngles {
public:
    LookAngles(const Satellite& sat, const ObserverLocation& observer, const QDateTime& startUtc)
        : m_sgp4(libsgp4::Tle(sat.name.toStdString(), sat.line1.toStdString(), sat.line2.toStdString()))
        // Observer altitude is stored in meters; libsgp4 expects kilometers
        , m_observer(observer.latitude, observer.longitude, observer.altitude / 1000.0)
    {
        QDateTime utc = startUtc.toUTC();
        QDate d = utc.date();
        QTime t = utc.time();
        m_start = libsgp4::DateTime(d.year(), d.month(), d.day(),
                                    t.hour(), t.minute(), t.second(), t.msec() * 1000);
    }

    // Returns elevation in degrees; azimuth (degrees) is written if requested.
    double elevation(double seconds, double* azimuth = nullptr) {
        libsgp4::Eci eci = m_sgp4.FindPosition(m_start.AddSeconds(seconds));
        libsgp4::CoordTopocentric topo = m_observer.GetLookAngle(eci);
        if (azimuth) *azimuth = libsgp4::Util::RadiansToDegrees(topo.azimuth);
        return libsgp4::Util::RadiansToDegrees(topo.elevation);
    }

    // Time of the horizon crossing in (below, above] or [above, below).
    double crossing(double below, double above) {
        while (std::abs(above - below) > kPrecisionSeconds) {
            double mid = (below + above) / 2.0;
            if (elevation(mid) > 0.0) above = mid; else below = mid;
        }
        return above;
    }

    // Time of maximum elevation in [a, b] (golden-section search).
    double peak(double a, double b) {
        const double invPhi = (std::sqrt(5.0) - 1.0) / 2.0;
        double c = b - invPhi * (b - a);
        double d = a + invPhi * (b - a);
        double ec = elevation(c), ed = elevation(d);
        while (b - a > kPrecisionSeconds) {
            if (ec > ed) { b = d; d = c; ed = ec; c = b - invPhi * (b - a); ec = elevation(c); }
            else         { a = c; c = d; ec = ed; d = a + invPhi * (b - a); ed = elevation(d); }
        }
        return (a + b) / 2.0;
    }

private:
    libsgp4::SGP4 m_sgp4;
    libsgp4::Observer m_observer;
    libsgp4::DateTime m_start;
};

QDateTime atOffset(const QDateTime& startUtc, double seconds) {
    return startUtc.addMSecs(qint64(std::llround(seconds * 1000.0)));
}
}

QList<SatellitePass> PassPredictor::predictPasses(const Satellite& sat,
                                                  const ObserverLocation& observer,
                                                  const QDateTime& startUtc,
                                                  double hours,
                                                  double minElevation) {
    QList<SatellitePass> passes;
    const double end = hours * 3600.0;

    try {
        LookAngles look(sat, observer, startUtc);

        // Already up: trace back to the actual rise.
        bool up = false;
        double riseAt = 0.0;
        bool riseBeforeSearch = false;
        if (look.elevation(0.0) > 0.0) {
            up = true;
            double t = 0.0;
            while (t > -kMaxPassSeconds && look.elevation(t - kStepSeconds) > 0.0) t -= kStepSeconds;
            if (t <= -kMaxPassSeconds) {
                riseAt = 0.0;
                riseBeforeSearch = true;
            } else {
                riseAt = look.crossing(t - kStepSeconds, t);
            }
        }

        double prev = 0.0;
        for (double t = kStepSeconds; ; t += kStepSeconds) {
            bool beyondWindow = t > end;
            if (beyondWindow && !up) break;

            double el = look.elevation(t);
            if (!up && el > 0.0) {
                up = true;
                riseBeforeSearch = false;
                riseAt = look.crossing(prev, t);
            } else if (up && (el <= 0.0 || t > end + kMaxPassSeconds)) {
                SatellitePass pass;
                bool setAfterSearch = el > 0.0;
                double setAt = setAfterSearch ? end : look.crossing(t, prev);
                double peakAt = look.peak(riseAt, setAt);

                pass.satelliteName = sat.name;
                pass.catalogNumber = sat.catalogNumber;
                pass.aos = atOffset(startUtc, riseAt);
                pass.tca = atOffset(startUtc, peakAt);
                pass.los = atOffset(startUtc, setAt);
                look.elevation(riseAt, &pass.aosAzimuth);
                pass.maxElevation = look.elevation(peakAt, &pass.tcaAzimuth);
                look.elevation(setAt, &pass.losAzimuth);
                pass.aosBeforeSearch = riseBeforeSearch;
                pass.losAfterSearch = setAfterSearch;

                if (pass.maxElevation >= minElevation) passes.append(pass);
                up = false;
                if (beyondWindow) break;
            }
            prev = t;
        }
    } catch (const std::exception&) {
        // Invalid TLE or decayed satellite: return whatever was found
    }

    return passes;
}

QList<SatellitePass> PassPredictor::predictAll(const QList<Satellite>& satellites,
                                               const ObserverLocation& observer,
                                               const QDateTime& startUtc,
                                               double hours,
                                               double minElevation) {
    QList<SatellitePass> all;
    for (const Satellite& sat : satellites) {
        all.append(predictPasses(sat, observer, startUtc, hours, minElevation));
    }
    std::sort(all.begin(), all.end(), [](const SatellitePass& a, const SatellitePass& b) {
        return a.aos < b.aos;
    });
    return all;
}

QList<QPointF> PassPredictor::passTrack(const Satellite& sat,
                                        const ObserverLocation& observer,
                                        const SatellitePass& pass,
                                        int stepSeconds) {
    QList<QPointF> track;
    try {
        LookAngles look(sat, observer, pass.aos);
        int duration = pass.durationSeconds();
        for (int t = 0; ; t += stepSeconds) {
            bool last = t >= duration;
            double az = 0.0;
            double el = look.elevation(last ? duration : t, &az);
            track.append(QPointF(az, std::max(el, 0.0)));
            if (last) break;
        }
    } catch (const std::exception&) {
    }
    return track;
}

QString PassPredictor::directionText(const SatellitePass& pass) {
    return QString("%1 → %2 → %3").arg(compassPoint(pass.aosAzimuth),
                                     compassPoint(pass.tcaAzimuth),
                                     compassPoint(pass.losAzimuth));
}

QString PassPredictor::formatDuration(qint64 seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds < 60) return QString("%1s").arg(seconds);
    if (seconds < 3600) return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    if (seconds < 86400) return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60, 2, 10, QChar('0'));
    return QString("%1d %2h").arg(seconds / 86400).arg((seconds % 86400) / 3600);
}

QString PassPredictor::compassPoint(double azimuth) {
    static const char* names[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    double a = std::fmod(std::fmod(azimuth, 360.0) + 360.0, 360.0);
    return names[int((a + 11.25) / 22.5) % 16];
}
