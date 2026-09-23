# SGP4 Integration Guide

> **Status: done.** libsgp4 (dnwrnr/sgp4) is now vendored in `external/sgp4` and used by
> `src/sgp4wrapper.cpp`. This guide is kept for reference only.

This document provides step-by-step instructions for replacing the simplified orbital propagation with a production-ready SGP4 implementation.

## Option 1: Using sgp4 by dnwrnr (Recommended)

### Installation

```bash
cd satellite-tracker
mkdir -p external
cd external
git clone https://github.com/dnwrnr/sgp4.git
cd sgp4
mkdir build && cd build
cmake ..
make
sudo make install
```

### Update CMakeLists.txt

```cmake
# Add after find_package(Qt6...)
find_package(SGP4 REQUIRED)

# Update target_link_libraries
target_link_libraries(${PROJECT_NAME} 
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    SGP4::SGP4
)
```

### Modify sgp4wrapper.h

```cpp
#ifndef SGP4WRAPPER_H
#define SGP4WRAPPER_H

#include "satellite.h"
#include <QDateTime>
#include <sgp4/sgp4.h>  // Add this

class SGP4Wrapper {
public:
    SGP4Wrapper();
    
    static SatellitePosition calculatePosition(const Satellite& sat,
                                               const QDateTime& time,
                                               const ObserverLocation& observer);
    
private:
    // Initialize satellite from TLE
    static bool initializeSatellite(const QString& line1, 
                                   const QString& line2,
                                   elsetrec& satrec);
    
    // Convert QDateTime to minutes since epoch
    static double calculateTimeSinceEpoch(const QDateTime& time,
                                         const elsetrec& satrec);
    
    // Convert TEME to ECEF coordinates
    static void temeToECEF(const QDateTime& time,
                          double r_teme[3], double v_teme[3],
                          double& x, double& y, double& z);
};

#endif
```

### Modify sgp4wrapper.cpp

```cpp
#include "sgp4wrapper.h"
#include <cmath>

SatellitePosition SGP4Wrapper::calculatePosition(const Satellite& sat,
                                                  const QDateTime& time,
                                                  const ObserverLocation& observer) {
    SatellitePosition pos;
    
    // Initialize SGP4 satellite record
    elsetrec satrec;
    if (!initializeSatellite(sat.line1, sat.line2, satrec)) {
        return pos; // Return empty position on error
    }
    
    // Calculate time since epoch
    double tsince = calculateTimeSinceEpoch(time, satrec);
    
    // Propagate orbit
    double r[3], v[3];  // TEME coordinates
    if (!sgp4(satrec, tsince, r, v)) {
        return pos; // Propagation failed
    }
    
    // Convert TEME to ECEF
    double satX, satY, satZ;
    temeToECEF(time, r, v, satX, satY, satZ);
    
    // Convert ECEF to geodetic (latitude, longitude, altitude)
    double lat, lon, alt;
    ecefToGeodetic(satX, satY, satZ, lat, lon, alt);
    
    pos.latitude = lat;
    pos.longitude = lon;
    pos.altitude = alt;
    
    // Calculate look angles from observer
    double obsX, obsY, obsZ;
    geodeticToECEF(observer.latitude, observer.longitude, 
                   observer.altitude / 1000.0, obsX, obsY, obsZ);
    
    double south, east, zenith;
    ecefToTopocentric(satX, satY, satZ, obsX, obsY, obsZ,
                     observer.latitude, observer.longitude,
                     south, east, zenith);
    
    topocentricToAzEl(south, east, zenith, 
                     pos.azimuth, pos.elevation, pos.range);
    
    // Calculate range rate
    double vx = v[0], vy = v[1], vz = v[2];
    double dx = satX - obsX;
    double dy = satY - obsY;
    double dz = satZ - obsZ;
    double range = sqrt(dx*dx + dy*dy + dz*dz);
    
    // Dot product of velocity and range vector
    pos.rangerate = (vx*dx + vy*dy + vz*dz) / range;
    
    return pos;
}

bool SGP4Wrapper::initializeSatellite(const QString& line1, 
                                     const QString& line2,
                                     elsetrec& satrec) {
    // Convert QString to C strings
    std::string l1 = line1.toStdString();
    std::string l2 = line2.toStdString();
    
    // Initialize SGP4
    double startmfe, stopmfe, deltamin;
    char typerun = 'c';  // catalog mode
    char typeinput = 'v'; // verification mode
    char opsmode = 'i';   // improved mode
    
    bool success = twoline2rv(
        l1.c_str(),
        l2.c_str(),
        typerun,
        typeinput, 
        opsmode,
        wgs84,        // Use WGS84 constants
        startmfe,
        stopmfe,
        deltamin,
        satrec
    );
    
    return success;
}

double SGP4Wrapper::calculateTimeSinceEpoch(const QDateTime& time,
                                           const elsetrec& satrec) {
    // Convert epoch to Julian date
    int year = satrec.epochyr;
    if (year < 57) year += 2000;
    else year += 1900;
    
    double epochJD = jday(year, 1, 0, 0, 0, 0.0) + satrec.epochdays;
    
    // Convert current time to Julian date
    QDateTime utc = time.toUTC();
    int yr = utc.date().year();
    int mo = utc.date().month();
    int day = utc.date().day();
    int hr = utc.time().hour();
    int min = utc.time().minute();
    double sec = utc.time().second() + utc.time().msec() / 1000.0;
    
    double currentJD = jday(yr, mo, day, hr, min, sec);
    
    // Return difference in minutes
    return (currentJD - epochJD) * 1440.0;
}

void SGP4Wrapper::temeToECEF(const QDateTime& time,
                            double r_teme[3], double v_teme[3],
                            double& x, double& y, double& z) {
    // Calculate Greenwich Mean Sidereal Time
    QDateTime utc = time.toUTC();
    int yr = utc.date().year();
    int mo = utc.date().month();
    int day = utc.date().day();
    int hr = utc.time().hour();
    int min = utc.time().minute();
    double sec = utc.time().second() + utc.time().msec() / 1000.0;
    
    double jd = jday(yr, mo, day, hr, min, sec);
    double gmst = gstime(jd);
    
    // Rotate from TEME to ECEF
    double cosGMST = cos(gmst);
    double sinGMST = sin(gmst);
    
    x = cosGMST * r_teme[0] + sinGMST * r_teme[1];
    y = -sinGMST * r_teme[0] + cosGMST * r_teme[1];
    z = r_teme[2];
}
```

## Option 2: Using Vallado's Reference Implementation

### Installation

```bash
cd satellite-tracker/external
wget https://celestrak.org/software/vallado/cpp.zip
unzip cpp.zip -d vallado-cpp
```

### Integration

Create wrapper for Vallado's code:

```cpp
// In sgp4wrapper.cpp
#include "../external/vallado-cpp/SGP4.h"

// Rest of implementation similar to Option 1
```

## Testing Your Integration

### Test 1: ISS Position Verification

```cpp
void testISSPosition() {
    // Known TLE for ISS (update with current data)
    QString line1 = "1 25544U 98067A   24024.50000000  .00012345  00000-0  12345-3 0  9991";
    QString line2 = "2 25544  51.6400 208.9163 0006317 356.8538 126.2099 15.54225995123456";
    
    Satellite iss;
    iss.name = "ISS (ZARYA)";
    iss.line1 = line1;
    iss.line2 = line2;
    
    // Use known time and location
    QDateTime testTime = QDateTime::fromString("2024-01-24T12:00:00Z", Qt::ISODate);
    ObserverLocation denver(39.7392, -104.9903, 1609.0);
    
    SatellitePosition pos = SGP4Wrapper::calculatePosition(iss, testTime, denver);
    
    // Compare with n2yo.com or heavens-above.com
    qDebug() << "ISS Position:";
    qDebug() << "  Latitude:" << pos.latitude;
    qDebug() << "  Longitude:" << pos.longitude;
    qDebug() << "  Altitude:" << pos.altitude;
    qDebug() << "  Azimuth:" << pos.azimuth;
    qDebug() << "  Elevation:" << pos.elevation;
}
```

### Test 2: Compare Multiple Satellites

```cpp
void compareMultipleSatellites() {
    // Test with amateur radio satellites
    QStringList satellites = {
        "ISS (ZARYA)",
        "AO-91",
        "SO-50",
        "AO-92"
    };
    
    for (const QString& satName : satellites) {
        // Fetch TLE, calculate position
        // Compare with online tracking
    }
}
```

## Accuracy Verification

Expected accuracy with proper SGP4:
- **Position**: ±1-5 km (LEO satellites)
- **Timing**: ±1-2 seconds for passes
- **Doppler**: ±100-500 Hz (depending on frequency)

### Factors Affecting Accuracy

1. **TLE Age**: Update TLEs daily for best accuracy
2. **Atmospheric Drag**: More significant for low altitude satellites
3. **Solar Activity**: Affects drag predictions
4. **Orbit Type**: GEO satellites more stable than LEO

## Troubleshooting

### Compilation Errors

```bash
# If SGP4 headers not found
export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH

# If library not found
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### Runtime Errors

**"SGP4 propagation failed"**
- TLE may be corrupted or too old
- Check TLE checksum validation
- Verify satellite hasn't decayed

**Positions seem wrong**
- Check time zone (must use UTC)
- Verify coordinate system (geodetic vs geocentric)
- Ensure TEME→ECEF rotation is correct

## Performance Considerations

With real SGP4:
- Each propagation: ~50-100 microseconds
- Can easily handle 100+ satellites at 1 Hz
- Consider caching if updating faster than 1 Hz

### Optimization

```cpp
// Cache satellite records (avoid re-parsing TLE)
QMap<int, elsetrec> m_satRecords;

void SatelliteTracker::updatePositions() {
    for (Satellite& sat : m_satellites) {
        if (!m_satRecords.contains(sat.catalogNumber)) {
            elsetrec satrec;
            SGP4Wrapper::initializeSatellite(sat.line1, sat.line2, satrec);
            m_satRecords[sat.catalogNumber] = satrec;
        }
        
        // Use cached record
        sat.position = SGP4Wrapper::calculatePosition(
            m_satRecords[sat.catalogNumber], 
            QDateTime::currentDateTimeUtc(), 
            m_observer
        );
    }
}
```

## Next Steps After Integration

1. **Add unit tests** for SGP4 calculations
2. **Implement pass prediction** using propagated positions
3. **Add Doppler calculations** for radio tracking
4. **Create ground track visualization**
5. **Add TLE update scheduling** (auto-fetch daily)

## References

- [Vallado & Crawford (2008) - SGP4 Revisited](https://celestrak.org/publications/AIAA/2006-6753/)
- [Revisiting Spacetrack Report #3](https://celestrak.org/publications/AIAA/2008-6770/)
- [CelesTrak SGP4 Documentation](https://celestrak.org/NORAD/documentation/)
