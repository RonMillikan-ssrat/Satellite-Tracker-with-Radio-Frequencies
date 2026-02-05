#ifndef SGP4WRAPPER_H
#define SGP4WRAPPER_H

#include "satellite.h"
#include <QDateTime>

class SGP4Wrapper {
public:
    SGP4Wrapper();
    
    // Calculate satellite position at given time from observer location
    // This is a simplified implementation - in production you'd use a full SGP4 library
    static SatellitePosition calculatePosition(const Satellite& sat,
                                               const QDateTime& time,
                                               const ObserverLocation& observer);
    
    // Calculate look angles (azimuth, elevation) from observer to satellite
    static void calculateLookAngles(const SatellitePosition& satPos,
                                    const ObserverLocation& observer,
                                    double& azimuth,
                                    double& elevation,
                                    double& range);
    
private:
    // Helper function to convert lat/lon/alt to ECEF coordinates
    static void geodeticToECEF(double lat, double lon, double alt,
                              double& x, double& y, double& z);
    
    // Helper function to convert ECEF to topocentric (SEZ) coordinates
    static void ecefToTopocentric(double satX, double satY, double satZ,
                                 double obsX, double obsY, double obsZ,
                                 double obsLat, double obsLon,
                                 double& south, double& east, double& zenith);
    
    // Helper function to convert topocentric to azimuth/elevation
    static void topocentricToAzEl(double south, double east, double zenith,
                                 double& azimuth, double& elevation, double& range);
};

#endif // SGP4WRAPPER_H
