#include "sgp4wrapper.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SGP4Wrapper::SGP4Wrapper() {}

SatellitePosition SGP4Wrapper::calculatePosition(const Satellite& sat,
                                                  const QDateTime& time,
                                                  const ObserverLocation& observer) {
    SatellitePosition pos;
    
    // NOTE: This is a SIMPLIFIED implementation for demonstration
    // For production use, integrate a real SGP4 library
    
    QString line2 = sat.line2;
    
    // Parse orbital elements from TLE
    double meanMotion = 14.0;
    if (line2.length() >= 63) {
        QString mmStr = line2.mid(52, 11).trimmed();
        bool ok;
        double mm = mmStr.toDouble(&ok);
        if (ok && mm > 0) meanMotion = mm;
    }
    
    double inclination = 51.6;
    if (line2.length() >= 16) {
        QString incStr = line2.mid(8, 8).trimmed();
        bool ok;
        double inc = incStr.toDouble(&ok);
        if (ok) inclination = inc;
    }
    
    double raan = 0.0;
    if (line2.length() >= 25) {
        QString raanStr = line2.mid(17, 8).trimmed();
        bool ok;
        double r = raanStr.toDouble(&ok);
        if (ok) raan = r;
    }
    
    double argPerigee = 0.0;
    if (line2.length() >= 42) {
        QString argStr = line2.mid(34, 8).trimmed();
        bool ok;
        double arg = argStr.toDouble(&ok);
        if (ok) argPerigee = arg;
    }
    
    // Calculate orbital parameters
    double n = meanMotion * 2.0 * M_PI / 86400.0; // rad/s
    const double mu = 398600.4418; // km^3/s^2
    double a = std::pow(mu / (n * n), 1.0/3.0); // km
    double altitude = a - 6371.0;
    
    // Time-based position
    qint64 secondsSinceEpoch = time.toSecsSinceEpoch();
    double phaseOffset = sat.catalogNumber * 0.1;
    double meanAnomaly = fmod(n * secondsSinceEpoch + phaseOffset, 2.0 * M_PI);
    
    // Convert angles to radians
    double incRad = inclination * M_PI / 180.0;
    double raanRad = raan * M_PI / 180.0;
    double argPerigeeRad = argPerigee * M_PI / 180.0;
    
    // Argument of latitude
    double u = meanAnomaly + argPerigeeRad;
    
    // Position in orbital plane
    double xOrb = a * cos(u);
    double yOrb = a * sin(u);
    
    // Rotation matrices to convert to ECI
    double cosRaan = cos(raanRad);
    double sinRaan = sin(raanRad);
    double cosInc = cos(incRad);
    double sinInc = sin(incRad);
    
    // ECI position
    double xEci = cosRaan * xOrb - sinRaan * cosInc * yOrb;
    double yEci = sinRaan * xOrb + cosRaan * cosInc * yOrb;
    double zEci = sinInc * yOrb;
    
    // Calculate GMST for Earth rotation
    double daysSinceJ2000 = (secondsSinceEpoch / 86400.0) - 10957.5;
    double gmst = fmod(280.46061837 + 360.98564736629 * daysSinceJ2000, 360.0);
    double gmstRad = gmst * M_PI / 180.0;
    
    // Convert ECI to ECEF
    double xEcef = xEci * cos(gmstRad) + yEci * sin(gmstRad);
    double yEcef = -xEci * sin(gmstRad) + yEci * cos(gmstRad);
    double zEcef = zEci;
    
    // Convert ECEF to geodetic coordinates
    double lon = atan2(yEcef, xEcef) * 180.0 / M_PI;
    double r = sqrt(xEcef * xEcef + yEcef * yEcef);
    double lat = atan2(zEcef, r) * 180.0 / M_PI;
    
    pos.latitude = lat;
    pos.longitude = lon;
    pos.altitude = altitude;
    
    // Calculate look angles from observer
    calculateLookAngles(pos, observer, pos.azimuth, pos.elevation, pos.range);
    
    // Calculate satellite velocity in ECI frame
    // For circular orbit: v = sqrt(mu/a)
    double orbitalSpeed = sqrt(mu / a); // km/s
    
    // Velocity vector in orbital plane (perpendicular to position)
    double vxOrb = -orbitalSpeed * sin(u);
    double vyOrb = orbitalSpeed * cos(u);
    
    // Transform velocity to ECI
    double vxEci = cosRaan * vxOrb - sinRaan * cosInc * vyOrb;
    double vyEci = sinRaan * vxOrb + cosRaan * cosInc * vyOrb;
    double vzEci = sinInc * vyOrb;
    
    // Transform velocity to ECEF (account for Earth rotation)
    double earthRotRate = 2.0 * M_PI / 86400.0; // rad/s
    double vxEcef = vxEci * cos(gmstRad) + vyEci * sin(gmstRad) + earthRotRate * (-xEcef * sin(gmstRad) + yEcef * cos(gmstRad));
    double vyEcef = -vxEci * sin(gmstRad) + vyEci * cos(gmstRad) + earthRotRate * (xEcef * cos(gmstRad) + yEcef * sin(gmstRad));
    double vzEcef = vzEci;
    
    // Calculate range rate (dot product of velocity with range vector)
    double obsX, obsY, obsZ;
    geodeticToECEF(observer.latitude, observer.longitude, observer.altitude / 1000.0,
                   obsX, obsY, obsZ);
    
    double rangeVecX = xEcef - obsX;
    double rangeVecY = yEcef - obsY;
    double rangeVecZ = zEcef - obsZ;
    double rangeDistance = sqrt(rangeVecX * rangeVecX + rangeVecY * rangeVecY + rangeVecZ * rangeVecZ);
    
    // Range rate = (velocity · range_vector) / range
    pos.rangerate = (vxEcef * rangeVecX + vyEcef * rangeVecY + vzEcef * rangeVecZ) / rangeDistance;
    
    return pos;
}

void SGP4Wrapper::calculateLookAngles(const SatellitePosition& satPos,
                                      const ObserverLocation& observer,
                                      double& azimuth,
                                      double& elevation,
                                      double& range) {
    // Convert observer to ECEF
    double obsX, obsY, obsZ;
    geodeticToECEF(observer.latitude, observer.longitude, observer.altitude / 1000.0,
                   obsX, obsY, obsZ);
    
    // Convert satellite to ECEF
    double satX, satY, satZ;
    geodeticToECEF(satPos.latitude, satPos.longitude, satPos.altitude + 6371.0,
                   satX, satY, satZ);
    
    // Convert to topocentric coordinates
    double south, east, zenith;
    ecefToTopocentric(satX, satY, satZ, obsX, obsY, obsZ,
                     observer.latitude, observer.longitude,
                     south, east, zenith);
    
    // Convert to azimuth/elevation
    topocentricToAzEl(south, east, zenith, azimuth, elevation, range);
}

void SGP4Wrapper::geodeticToECEF(double lat, double lon, double alt,
                                  double& x, double& y, double& z) {
    const double a = 6378.137; // Earth semi-major axis (km)
    const double f = 1.0 / 298.257223563; // Flattening
    const double e2 = 2.0 * f - f * f; // Eccentricity squared
    
    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    
    double sinLat = sin(latRad);
    double cosLat = cos(latRad);
    double N = a / sqrt(1.0 - e2 * sinLat * sinLat);
    
    x = (N + alt) * cosLat * cos(lonRad);
    y = (N + alt) * cosLat * sin(lonRad);
    z = (N * (1.0 - e2) + alt) * sinLat;
}

void SGP4Wrapper::ecefToTopocentric(double satX, double satY, double satZ,
                                     double obsX, double obsY, double obsZ,
                                     double obsLat, double obsLon,
                                     double& south, double& east, double& zenith) {
    // Vector from observer to satellite
    double dx = satX - obsX;
    double dy = satY - obsY;
    double dz = satZ - obsZ;
    
    double latRad = obsLat * M_PI / 180.0;
    double lonRad = obsLon * M_PI / 180.0;
    
    double sinLat = sin(latRad);
    double cosLat = cos(latRad);
    double sinLon = sin(lonRad);
    double cosLon = cos(lonRad);
    
    // Rotation matrix to topocentric (SEZ) coordinates
    south = -sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz;
    east = -sinLon * dx + cosLon * dy;
    zenith = cosLat * cosLon * dx + cosLat * sinLon * dy + sinLat * dz;
}

void SGP4Wrapper::topocentricToAzEl(double south, double east, double zenith,
                                     double& azimuth, double& elevation, double& range) {
    range = sqrt(south * south + east * east + zenith * zenith);
    elevation = asin(zenith / range) * 180.0 / M_PI;
    azimuth = atan2(east, south) * 180.0 / M_PI;
    
    // Convert azimuth from South=0 to North=0
    azimuth = fmod(azimuth + 180.0, 360.0);
    if (azimuth < 0) azimuth += 360.0;
}
