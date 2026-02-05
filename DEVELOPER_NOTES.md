# Developer Notes

## Architecture Overview

### Data Flow

```
User Location → SatelliteTracker → SGP4Wrapper → Satellite Positions
                      ↓
                  MainWindow
                   ↓     ↓
              SkyMapWidget  QTableWidget
```

### Key Classes

#### SatelliteTracker
- Manages satellite data and observer location
- Fetches TLE data from CelesTrak
- Coordinates position updates
- Emits signals for GUI updates

#### SGP4Wrapper
**Current Status**: Simplified implementation
**What it does**: Basic circular orbit approximation
**What it should do**: Full SGP4 propagation

The current implementation is a placeholder. It demonstrates the interface but does NOT provide accurate satellite positions.

#### SkyMapWidget
- Polar projection sky map (zenith at center)
- Draws compass directions and elevation circles
- Plots satellites with color coding

#### MainWindow
- Coordinates all GUI components
- Handles user input
- Updates display on timer

## Upgrading to Production SGP4

### Recommended Library: sgp4 by dnwrnr

```bash
# Clone the library
git clone https://github.com/dnwrnr/sgp4.git external/sgp4

# Update CMakeLists.txt
add_subdirectory(external/sgp4)
target_link_libraries(${PROJECT_NAME} sgp4)
```

### Integration Steps

1. **Replace SGP4Wrapper implementation**

```cpp
#include <sgp4/sgp4.h>

SatellitePosition SGP4Wrapper::calculatePosition(
    const Satellite& sat,
    const QDateTime& time,
    const ObserverLocation& observer) {
    
    // Parse TLE into SGP4 format
    elsetrec satrec;
    double startmfe, stopmfe, deltamin;
    twoline2rv(sat.line1.toStdString().c_str(),
               sat.line2.toStdString().c_str(),
               'c', 'i', 'e',
               startmfe, stopmfe, deltamin, satrec);
    
    // Calculate minutes since epoch
    double tsince = calculateTSince(time, satrec);
    
    // Propagate
    double r[3], v[3];
    sgp4(satrec, tsince, r, v);
    
    // Convert to lat/lon/alt
    // ... implementation ...
    
    return pos;
}
```

2. **Add proper time handling**

```cpp
double calculateTSince(const QDateTime& time, const elsetrec& satrec) {
    // Convert epoch to QDateTime
    // Calculate difference in minutes
    // Return tsince value
}
```

3. **Test with known satellites**

Use ISS as a reference - compare with:
- https://www.n2yo.com
- https://www.heavens-above.com

## Radio Features Extension

### Doppler Shift Calculation

```cpp
double calculateDopplerShift(const Satellite& sat, 
                            double frequency) {
    // radial velocity (km/s) to frequency shift
    double c = 299792.458; // speed of light (km/s)
    double doppler = frequency * (sat.position.rangerate / c);
    return doppler;
}
```

### Transponder Support

```cpp
struct Transponder {
    QString name;
    double uplinkFreq;    // MHz
    double downlinkFreq;  // MHz
    QString mode;         // "FM", "SSB", "CW", etc.
    bool inverting;       // true for inverting transponders
};

class Satellite {
    // ... existing members ...
    QList<Transponder> transponders;
};
```

### Usage in GUI

```cpp
// Add to table
double uplinkDoppler = calculateDopplerShift(sat, 
    sat.transponders[0].uplinkFreq * 1e6);
double correctedUplink = sat.transponders[0].uplinkFreq - 
    (uplinkDoppler / 1e6);
```

## Pass Prediction

### Algorithm Overview

1. Propagate satellite forward in time
2. Calculate elevation at each step
3. Detect rise (elevation crosses 0°)
4. Continue until set (elevation crosses 0° again)
5. Find maximum elevation during pass

### Implementation

```cpp
struct Pass {
    QDateTime riseTime;
    QDateTime setTime;
    QDateTime maxElevationTime;
    double maxElevation;
    double riseAzimuth;
    double setAzimuth;
};

QList<Pass> predictPasses(const Satellite& sat,
                         const ObserverLocation& obs,
                         const QDateTime& start,
                         const QDateTime& end) {
    QList<Pass> passes;
    // ... implementation ...
    return passes;
}
```

## Performance Optimization

### Multi-threading Satellite Updates

```cpp
// In SatelliteTracker
QThreadPool* m_threadPool;

void SatelliteTracker::updatePositions() {
    QDateTime now = QDateTime::currentDateTimeUtc();
    
    for (Satellite& sat : m_satellites) {
        // Create worker for each satellite
        auto* worker = new SatelliteUpdateWorker(sat, now, m_observer);
        connect(worker, &SatelliteUpdateWorker::updated,
                this, &SatelliteTracker::onSatelliteUpdated);
        m_threadPool->start(worker);
    }
}
```

### Caching Ground Tracks

```cpp
// Pre-calculate ground track for next hour
QList<QPointF> calculateGroundTrack(const Satellite& sat,
                                    int minutes = 60,
                                    int step = 1) {
    QList<QPointF> track;
    QDateTime now = QDateTime::currentDateTimeUtc();
    
    for (int i = 0; i < minutes; i += step) {
        auto pos = SGP4Wrapper::calculatePosition(sat, 
            now.addSecs(i * 60), ObserverLocation());
        track.append(QPointF(pos.longitude, pos.latitude));
    }
    
    return track;
}
```

## Testing Strategy

### Unit Tests

```cpp
// Test TLE parsing
void testTLEParser() {
    QString tle = "ISS (ZARYA)\n"
                  "1 25544U ...\n"
                  "2 25544 ...";
    QList<Satellite> sats = TLEParser::parseTLEData(tle);
    QCOMPARE(sats.size(), 1);
    QCOMPARE(sats[0].name, "ISS (ZARYA)");
}

// Test coordinate conversions
void testCoordinateConversion() {
    double lat = 39.7392, lon = -104.9903, alt = 1.609;
    double x, y, z;
    SGP4Wrapper::geodeticToECEF(lat, lon, alt, x, y, z);
    // Verify against known values
}
```

### Integration Tests

1. Compare predictions with known satellite tracking services
2. Verify Doppler calculations with radio operators
3. Test pass predictions against published schedules

## Deployment

### macOS App Bundle

```bash
# After building
macdeployqt SatelliteTracker.app -dmg
```

### Windows Installer

Use NSIS or WiX to create installer

### Linux AppImage

```bash
# Use linuxdeployqt
linuxdeployqt SatelliteTracker -appimage
```

## Common Issues

### Qt Not Found
```bash
export CMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
```

### Network Errors
- Check firewall settings
- Verify internet connection
- CelesTrak may rate-limit requests

### Inaccurate Positions
- Current SGP4 is simplified!
- Integrate real SGP4 library
- Check TLE data freshness (update daily)

## Resources

- [SGP4 Documentation](https://celestrak.org/NORAD/documentation/)
- [Qt Documentation](https://doc.qt.io)
- [AMSAT Frequency Guide](https://www.amsat.org/frequency-summary/)
- [Amateur Satellite Tracking](https://www.amsat.org)
